#include "bagel_texture_streamer.hpp"

#include "stb_image.h"   // implementation lives in bagel_textures.cpp

#include <cstring>
#include <iostream>

namespace bagel {

	BGLTextureStreamer::BGLTextureStreamer(BGLDevice& device, size_t residentCap, VkFormat format)
		: device_(device), cap_(residentCap == 0 ? 1 : residentCap), format_(format)
	{
		createSampler();

		threaded_    = device_.hasUploadQueue();
		uploadQueue_ = device_.uploadQueue();   // dedicated 2nd queue, or graphics on fallback

		// Own command pool on the graphics family. Used by the worker thread (threaded_) or by the
		// main thread in the sync fallback -- never both, never concurrently.
		VkCommandPoolCreateInfo pci{};
		pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pci.queueFamilyIndex = device_.findPhysicalQueueFamilies().graphicsFamily;
		pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK(vkCreateCommandPool(BGLDevice::device(), &pci, nullptr, &pool_));

		if (threaded_)
			worker_ = std::thread(&BGLTextureStreamer::workerLoop, this);
		else
			std::cout << "[BGLTextureStreamer] no dedicated upload queue; using sync main-thread uploads\n";
	}

	BGLTextureStreamer::~BGLTextureStreamer()
	{
		if (threaded_) {
			stop_ = true;
			reqCv_.notify_all();
			if (worker_.joinable()) worker_.join();
		}
		VkDevice dev = BGLDevice::device();
		vkDeviceWaitIdle(dev);

		// Any uploads that finished but were never published.
		for (Uploaded& u : ready_) if (u.ok) destroyGpu(u.image, u.memory, u.view);
		for (auto& kv : resident_) destroyGpu(kv.second.image, kv.second.memory, kv.second.tex.view);
		for (PendingDelete& d : pendingDeletes_) destroyGpu(d.image, d.memory, d.view);

		if (sampler_) vkDestroySampler(dev, sampler_, nullptr);
		if (pool_)    vkDestroyCommandPool(dev, pool_, nullptr);
	}

	void BGLTextureStreamer::createSampler()
	{
		VkSamplerCreateInfo s{};
		s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		s.magFilter = VK_FILTER_LINEAR;
		s.minFilter = VK_FILTER_LINEAR;
		s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		s.maxLod = 0.0f;
		VK_CHECK(vkCreateSampler(BGLDevice::device(), &s, nullptr, &sampler_));
	}

	void BGLTextureStreamer::request(const std::string& name, const std::string& pngPath)
	{
		if (resident_.count(name) || pending_.count(name)) return;   // already have it / in flight
		pending_.insert(name);
		{
			std::lock_guard<std::mutex> lk(reqMutex_);
			requests_.push_back({name, pngPath});
		}
		if (threaded_) reqCv_.notify_one();
	}

	const BGLTextureStreamer::ResidentTexture* BGLTextureStreamer::get(const std::string& name)
	{
		auto it = resident_.find(name);
		if (it == resident_.end()) return nullptr;
		lru_.erase(it->second.lru);                // move to most-recently-used
		lru_.push_front(name);
		it->second.lru = lru_.begin();
		return &it->second.tex;
	}

	void BGLTextureStreamer::beginFrame()
	{
		++frame_;

		// Sync fallback: no dedicated queue, so decode + upload a few per frame on this thread.
		if (!threaded_) {
			std::vector<Request> batch;
			{
				std::lock_guard<std::mutex> lk(reqMutex_);
				for (uint32_t i = 0; i < kSyncPerFrame && !requests_.empty(); ++i) {
					batch.push_back(std::move(requests_.front()));
					requests_.pop_front();
				}
			}
			if (!batch.empty()) uploadBatch(batch, uploadQueue_);
		}

		// Publish finished uploads into the resident set.
		std::vector<Uploaded> done;
		{
			std::lock_guard<std::mutex> lk(readyMutex_);
			done.swap(ready_);
		}
		for (Uploaded& u : done) {
			pending_.erase(u.name);
			if (!u.ok) continue;
			if (resident_.count(u.name)) { scheduleDelete(u.image, u.memory, u.view); continue; } // dup
			lru_.push_front(u.name);
			Resident r;
			r.tex = { u.view, sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, u.w, u.h };
			r.image = u.image;
			r.memory = u.memory;
			r.lru = lru_.begin();
			resident_.emplace(u.name, std::move(r));
		}

		// Enforce the resident cap (evict least-recently-used).
		while (resident_.size() > cap_ && !lru_.empty())
			evict(lru_.back());

		// Retire deferred deletes whose frame delay has elapsed.
		for (auto it = pendingDeletes_.begin(); it != pendingDeletes_.end();) {
			if (it->deleteAtFrame <= frame_) {
				destroyGpu(it->image, it->memory, it->view);
				it = pendingDeletes_.erase(it);
			} else ++it;
		}
	}

	void BGLTextureStreamer::evict(const std::string& name)
	{
		auto it = resident_.find(name);
		if (it == resident_.end()) return;
		if (onEvict_) onEvict_(name);                        // let consumer drop its derived handle
		lru_.erase(it->second.lru);
		scheduleDelete(it->second.image, it->second.memory, it->second.tex.view);
		resident_.erase(it);
	}

	void BGLTextureStreamer::scheduleDelete(VkImage image, VkDeviceMemory memory, VkImageView view)
	{
		pendingDeletes_.push_back({ image, memory, view, frame_ + kDeleteDelayFrames });
	}

	void BGLTextureStreamer::destroyGpu(VkImage image, VkDeviceMemory memory, VkImageView view)
	{
		VkDevice dev = BGLDevice::device();
		if (view)   vkDestroyImageView(dev, view, nullptr);
		if (image)  vkDestroyImage(dev, image, nullptr);
		if (memory) vkFreeMemory(dev, memory, nullptr);
	}

	void BGLTextureStreamer::workerLoop()
	{
		while (true) {
			std::vector<Request> batch;
			{
				std::unique_lock<std::mutex> lk(reqMutex_);
				reqCv_.wait(lk, [&] { return stop_.load() || !requests_.empty(); });
				if (stop_.load() && requests_.empty()) return;
				for (uint32_t i = 0; i < kBatch && !requests_.empty(); ++i) {
					batch.push_back(std::move(requests_.front()));
					requests_.pop_front();
				}
			}
			uploadBatch(batch, uploadQueue_);
		}
	}

	// Decode each PNG (stb) and upload the whole batch in ONE command buffer + submit + fence.
	// Runs on the worker thread (dedicated queue) or the main thread (sync fallback). Results are
	// pushed to ready_ for beginFrame() to publish. Uses its own pool_ and a per-batch fence, so
	// it never touches the graphics queue's global wait-idle path.
	void BGLTextureStreamer::uploadBatch(const std::vector<Request>& batch, VkQueue queue)
	{
		VkDevice dev = BGLDevice::device();

		struct Staged {
			std::string name;
			bool ok = false;
			VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
			VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
			uint32_t w = 0, h = 0;
		};
		std::vector<Staged> staged;
		staged.reserve(batch.size());

		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkCommandBufferAllocateInfo ai{};
		ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		ai.commandPool = pool_;
		ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ai.commandBufferCount = 1;
		VK_CHECK(vkAllocateCommandBuffers(dev, &ai, &cmd));

		VkCommandBufferBeginInfo bi{};
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

		auto barrier = [&](VkImage img, VkImageLayout oldL, VkImageLayout newL,
		                   VkAccessFlags srcA, VkAccessFlags dstA,
		                   VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
			VkImageMemoryBarrier b{};
			b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.oldLayout = oldL; b.newLayout = newL;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image = img;
			b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			b.srcAccessMask = srcA; b.dstAccessMask = dstA;
			vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
		};

		for (const Request& r : batch) {
			Staged s; s.name = r.name;
			int w = 0, h = 0, ch = 0;
			stbi_uc* pixels = stbi_load(r.path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
			if (!pixels || w <= 0 || h <= 0) { if (pixels) stbi_image_free(pixels); staged.push_back(std::move(s)); continue; }
			s.w = static_cast<uint32_t>(w); s.h = static_cast<uint32_t>(h);
			const VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;

			device_.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			                     s.staging, s.stagingMem);
			void* mapped = nullptr;
			vkMapMemory(dev, s.stagingMem, 0, size, 0, &mapped);
			std::memcpy(mapped, pixels, static_cast<size_t>(size));
			vkUnmapMemory(dev, s.stagingMem);
			stbi_image_free(pixels);

			VkImageCreateInfo ici{};
			ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType = VK_IMAGE_TYPE_2D;
			ici.format = format_;
			ici.extent = { s.w, s.h, 1 };
			ici.mipLevels = 1;
			ici.arrayLayers = 1;
			ici.samples = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling = VK_IMAGE_TILING_OPTIMAL;
			ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			device_.createImageWithInfo(ici, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s.image, s.memory);

			barrier(s.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			        0, VK_ACCESS_TRANSFER_WRITE_BIT,
			        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			VkBufferImageCopy region{};
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageExtent = { s.w, s.h, 1 };
			vkCmdCopyBufferToImage(cmd, s.staging, s.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			barrier(s.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

			VkImageViewCreateInfo vci{};
			vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			vci.image = s.image;
			vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			vci.format = format_;
			vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			VK_CHECK(vkCreateImageView(dev, &vci, nullptr, &s.view));

			s.ok = true;
			staged.push_back(std::move(s));
		}

		VK_CHECK(vkEndCommandBuffer(cmd));

		VkFence fence = VK_NULL_HANDLE;
		VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		VK_CHECK(vkCreateFence(dev, &fci, nullptr, &fence));

		VkSubmitInfo si{};
		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cmd;
		VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
		vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

		vkDestroyFence(dev, fence, nullptr);
		vkFreeCommandBuffers(dev, pool_, 1, &cmd);
		for (Staged& s : staged) {
			if (s.staging)    vkDestroyBuffer(dev, s.staging, nullptr);
			if (s.stagingMem) vkFreeMemory(dev, s.stagingMem, nullptr);
		}

		{
			std::lock_guard<std::mutex> lk(readyMutex_);
			for (Staged& s : staged) {
				Uploaded u; u.name = s.name; u.ok = s.ok;
				if (s.ok) { u.image = s.image; u.memory = s.memory; u.view = s.view; u.w = s.w; u.h = s.h; }
				ready_.push_back(std::move(u));
			}
		}
	}

} // namespace bagel
