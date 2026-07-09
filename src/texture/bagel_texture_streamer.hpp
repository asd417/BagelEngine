#pragma once
// Engine-level asynchronous texture streamer with a bounded resident set.
//
// Fills GPU textures from image files (PNG/stb) on a background thread and hands back the raw
// GPU resources (VkImageView + VkSampler + layout), so any consumer can use them: ImGui
// (ImGui_ImplVulkan_AddTexture), bindless shader sampling (register into the bindless manager),
// or anything else. Keeps at most `residentCap` textures live and "forgets" (LRU-evicts) the
// rest, so browsing thousands of images stays within a fixed VRAM budget.
//
// Threading: one upload thread decodes + uploads in batches, submitting on the device's
// dedicated upload queue (2nd graphics-family queue) so the main render thread never blocks.
// If no upload queue is available it falls back to synchronous main-thread uploads (a few per
// frame, drained in beginFrame). GPU resource creation (vkCreateImage/Buffer) is thread-safe;
// per-thread command pool; the only main-thread-only Vulkan call from consumers is turning a
// ResidentTexture into their own handle (that lives at the call site, not here).
//
// Usage (main thread):
//   streamer.beginFrame();                    // once per frame: publish uploads, retire deletes
//   if (auto* t = streamer.get(name)) { ...use t->view/sampler... } else streamer.request(name, path);
//   streamer.setOnEvict([](const std::string& n){ /* free your derived handle for n */ });

#include "engine/bagel_engine_device.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bagel {

	class BGLTextureStreamer {
	public:
		// The raw GPU resources for a resident texture. `sampler` is shared across all textures.
		struct ResidentTexture {
			VkImageView   view    = VK_NULL_HANDLE;
			VkSampler     sampler = VK_NULL_HANDLE;
			VkImageLayout layout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			uint32_t      width   = 0;
			uint32_t      height  = 0;
		};

		BGLTextureStreamer(BGLDevice& device, size_t residentCap = 512,
		                   VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
		~BGLTextureStreamer();
		BGLTextureStreamer(const BGLTextureStreamer&) = delete;
		BGLTextureStreamer& operator=(const BGLTextureStreamer&) = delete;

		// Queue an async load of `pngPath` under `name`. No-op if already resident or in flight.
		void request(const std::string& name, const std::string& pngPath);

		// Resident texture for `name`, or nullptr if not loaded yet. Touches LRU (marks it used
		// this frame, so it won't be evicted). The pointer is valid until the next beginFrame().
		const ResidentTexture* get(const std::string& name);

		bool isPending(const std::string& name) const { return pending_.count(name) != 0; }

		// Explicitly evict `name` now (fires onEvict, schedules its GPU resources for deletion).
		void forget(const std::string& name) { evict(name); }

		// Call once per frame on the main thread: publishes finished uploads into the resident
		// set, enforces the cap (LRU), and retires deferred GPU deletes. Drives the sync fallback.
		void beginFrame();

		// Fires (main thread, from beginFrame/forget/dtor-free path) just before a texture's GPU
		// resources are released, so a consumer can free its DERIVED handle for `name`
		// (ImTextureID / bindless slot). The generic "forget" hook.
		void setOnEvict(std::function<void(const std::string& name)> cb) { onEvict_ = std::move(cb); }

		size_t residentCount() const { return resident_.size(); }
		size_t residentCap()   const { return cap_; }

	private:
		struct Request { std::string name, path; };
		struct Uploaded {                     // worker -> main
			std::string    name;
			bool           ok = false;
			VkImage        image  = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView    view   = VK_NULL_HANDLE;
			uint32_t       w = 0, h = 0;
		};
		struct Resident {
			ResidentTexture tex;
			VkImage         image  = VK_NULL_HANDLE;
			VkDeviceMemory  memory = VK_NULL_HANDLE;
			std::list<std::string>::iterator lru;
		};
		struct PendingDelete {
			VkImage        image;
			VkDeviceMemory memory;
			VkImageView    view;
			uint64_t       deleteAtFrame;
		};

		void createSampler();
		void workerLoop();
		void uploadBatch(const std::vector<Request>& batch, VkQueue queue);  // decode + batched submit
		void evict(const std::string& name);                                 // main thread
		void scheduleDelete(VkImage, VkDeviceMemory, VkImageView);
		void destroyGpu(VkImage, VkDeviceMemory, VkImageView);

		BGLDevice& device_;
		size_t     cap_;
		VkFormat   format_;
		VkSampler  sampler_ = VK_NULL_HANDLE;

		bool          threaded_    = false;
		VkQueue       uploadQueue_ = VK_NULL_HANDLE;   // dedicated (or graphics on fallback)
		VkCommandPool pool_        = VK_NULL_HANDLE;    // used only by the upload path (worker or sync)
		std::thread   worker_;
		std::atomic<bool> stop_{false};

		std::mutex               reqMutex_;
		std::condition_variable  reqCv_;
		std::deque<Request>      requests_;

		std::mutex               readyMutex_;
		std::vector<Uploaded>    ready_;

		// main-thread only:
		std::unordered_map<std::string, Resident> resident_;
		std::list<std::string>                    lru_;       // front = most recently used
		std::unordered_set<std::string>           pending_;   // requested, not yet resident
		std::vector<PendingDelete>                pendingDeletes_;
		uint64_t                                  frame_ = 0;
		std::function<void(const std::string&)>   onEvict_;

		static constexpr uint32_t kBatch = 16;               // images decoded/uploaded per submit
		static constexpr uint32_t kSyncPerFrame = 4;         // fallback uploads per frame
		static constexpr uint32_t kDeleteDelayFrames = 3;    // > MAX_FRAMES_IN_FLIGHT (2)
	};

} // namespace bagel
