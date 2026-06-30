#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#include "bagel_textures.hpp"
#include "bagel_engine_swap_chain.hpp"
#include "bagel_util.hpp"
#include "bagel_imgui.hpp"
#include "bagel_engine_config.hpp"

// vulkan headers
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <array>
#include <limits>
#include <vector>

#include "bagel_imgui.hpp"
#define CONSOLE ConsoleApp::Instance()

//https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/api/texture_loading/texture_loading.cpp
namespace bagel {
	
	BGLTexture::BGLTexture(BGLDevice& device, VkFormat format) : bglDevice{ device }, imageFormat{ format } {}
	BGLTexture::~BGLTexture()
	{
		std::cout << "Destroying BGLTexture\n";
		vkDestroyImageView(BGLDevice::device(), info.view, nullptr);
		vkDestroyImage(BGLDevice::device(), info.image, nullptr);
		vkDestroySampler(BGLDevice::device(), info.sampler, nullptr);
		vkFreeMemory(BGLDevice::device(), info.device_memory, nullptr);
	}

	std::unique_ptr<BGLTexture> BGLTexture::createTextureFromFile(BGLDevice& bglDevice, const std::string & filepath, VkFormat imageFormat)
	{
		std::unique_ptr<BGLTexture> texture = std::make_unique<BGLTexture>(bglDevice, imageFormat);
		KTX_error_code result;
		ktxTexture* ktx_texture;
		result = ktxTexture_CreateFromNamedFile(filepath.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);

		if (ktx_texture == nullptr)
		{
			throw std::runtime_error("Couldn't load texture");
		}
		texture->info.width = ktx_texture->baseWidth;
		texture->info.height = ktx_texture->baseHeight;
		texture->info.mip_levels = ktx_texture->numLevels;
		//std::cout << " image has " << ktx_texture->numLevels << " mipmaps\n";
		//std::cout << " image WxH " << ktx_texture->baseWidth << "x" << ktx_texture->baseHeight << "\n";

		ktx_uint8_t* ktx_image_data = ktx_texture->pData;
		ktx_size_t   ktx_texture_size = ktx_texture->dataSize;
		
		std::unique_ptr<BGLBuffer> stagingBuffer = std::make_unique<BGLBuffer>(
			bglDevice,
			1,
			static_cast<uint32_t>(ktx_texture_size),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		stagingBuffer->map();
		stagingBuffer->writeToBuffer(ktx_image_data);

		//	std::cout << "Freed raw pixel data\n";
		populateBufferCopyRegionKTX(texture->buffer_copy_regions, ktx_texture, texture->info.mip_levels);

		//ktx_texture no longer needed
		ktxTexture_Destroy(ktx_texture);

		VkImageCreateInfo image_create_info{};
		{
		image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_create_info.imageType = VK_IMAGE_TYPE_2D;
		image_create_info.format = imageFormat;
		image_create_info.mipLevels = texture->info.mip_levels;
		image_create_info.arrayLayers = 1;
		image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		// Set initial layout of the image to undefined
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_create_info.extent = { texture->info.width, texture->info.height, 1 };
		image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		bglDevice.createImageWithInfo(image_create_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture->info.image, texture->info.device_memory);
		// Image memory barriers for the texture image
		// The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
		VkImageSubresourceRange subresource_range = {};
		{
			subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresource_range.baseMipLevel = 0;
			subresource_range.levelCount = texture->info.mip_levels;
			subresource_range.layerCount = 1;
		}

		// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
		VkImageMemoryBarrier image_memory_barrier;
		{
			image_memory_barrier.pNext = nullptr;
			image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			image_memory_barrier.image = texture->info.image;
			image_memory_barrier.subresourceRange = subresource_range;
			image_memory_barrier.srcAccessMask = 0;
			image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}

		//Record the command that copies from staging buffer to VkImage
		VkCommandBuffer copy_command = bglDevice.beginSingleTimeCommands();

		vkCmdPipelineBarrier(
			copy_command,
			VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &image_memory_barrier);

		// Copy mip levels from staging buffer
		VkBuffer buffer = stagingBuffer->getBuffer();
		vkCmdCopyBufferToImage(
			copy_command,
			buffer,
			texture->info.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(texture->buffer_copy_regions.size()),
			texture->buffer_copy_regions.data());

		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		{
			image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
		// Destination pipeline stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
		vkCmdPipelineBarrier(
			copy_command,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &image_memory_barrier);

		bglDevice.endSingleTimeCommands(copy_command);
		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		texture->info.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//Finished loading texture.
		// TODO: use fence to wait for image upload and then delete the staging buffer.
		// Staging buffer does not need to be kept after the copy command has executed
		
		// Also clearing out buffer_copy regions
		// texture->buffer_copy_regions.clear();

		// Create a texture sampler
		// In Vulkan textures are accessed by samplers
		// This separates all the sampling information from the texture data. This means you could have multiple sampler objects for the same texture with different settings
		// Note: Similar to the samplers available with OpenGL 3.3
		VkSamplerCreateInfo sampler{};
		sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;

		// We used staging buffer therefore set the maxlod as the number of mipmaps
		// If not, must be set to 0
		// Set max level-of-detail to mip level count of the texture
		sampler.maxLod = static_cast<float>(texture->info.mip_levels);
		// Enable anisotropic filtering
		// This feature is optional, so we must check if it's supported on the device
		if (bglDevice.supportedFeatures.samplerAnisotropy)
		{
			// Use max. level of anisotropy for this example
			sampler.maxAnisotropy = bglDevice.properties.limits.maxSamplerAnisotropy;
			sampler.anisotropyEnable = VK_TRUE;
		}
		else
		{
			// The device does not support anisotropic filtering
			sampler.maxAnisotropy = 1.0;
			sampler.anisotropyEnable = VK_FALSE;
		}
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VkResult Samplerresult = vkCreateSampler(BGLDevice::device(), &sampler, nullptr, &texture->info.sampler);
		if (Samplerresult != VK_SUCCESS)
		{                                                               
			std::cout << "Detected Vulkan error: " << Samplerresult << std::endl;
			abort();                                                   
		}

		// Create image view
		// Textures are not directly accessed by the shaders and
		// are abstracted by image views containing additional
		// information and sub resource ranges
		VkImageViewCreateInfo view{};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = imageFormat;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
		// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;
		// Linear tiling usually won't support mip maps
		// Only set mip map count if optimal tiling is used
		view.subresourceRange.levelCount = texture->info.mip_levels;
		// The view will be based on the texture's image
		view.image = texture->info.image;
		VK_CHECK(vkCreateImageView(BGLDevice::device(), &view, nullptr, &texture->info.view));

		return texture;
	}

	void populateBufferCopyRegionSTB(std::vector<VkBufferImageCopy>& buffer_copy_regions, uint32_t width, uint32_t height)
	{

		VkBufferImageCopy buffer_copy_region = {};
		buffer_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		buffer_copy_region.imageSubresource.mipLevel = 0;
		buffer_copy_region.imageSubresource.baseArrayLayer = 0;
		buffer_copy_region.imageSubresource.layerCount = 1;
		buffer_copy_region.imageExtent.width = width; //divides the texture width by pow(2,i)
		buffer_copy_region.imageExtent.height = height;
		buffer_copy_region.imageExtent.depth = 1;
		buffer_copy_region.bufferOffset = 0;
		buffer_copy_region.bufferRowLength = 0;
		buffer_copy_regions.push_back(buffer_copy_region);

	}

	void populateBufferCopyRegionKTX(std::vector<VkBufferImageCopy> &buffer_copy_regions, ktxTexture* ktx_texture, uint32_t mip_levels)
	{
		// Setup buffer copy regions for each mip level
		for (uint32_t i = 0; i < mip_levels; i++)
		{
			//std::cout << "Mipmap index: " << i << "\n";
			VkBufferImageCopy buffer_copy_region = {};
			buffer_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			buffer_copy_region.imageSubresource.mipLevel = i;
			buffer_copy_region.imageSubresource.baseArrayLayer = 0;
			buffer_copy_region.imageSubresource.layerCount = 1;
			buffer_copy_region.imageExtent.width = ktx_texture->baseWidth >> i; //divides the texture width by pow(2,i)
			buffer_copy_region.imageExtent.height = ktx_texture->baseHeight >> i;
			buffer_copy_region.imageExtent.depth = 1;
			ktx_size_t        offset;
			KTX_error_code    result = ktxTexture_GetImageOffset(ktx_texture, i, 0, 0, &offset);
			buffer_copy_region.bufferOffset = offset;
			buffer_copy_regions.push_back(buffer_copy_region);

			//std::cout << "	offset: " << offset << " WxH: " << buffer_copy_region.imageExtent.width << "x" << buffer_copy_region.imageExtent.height << "\n";
		}
	}

	BGLTextureLoader::BGLTextureLoader(
		BGLDevice& _bglDevice,
		BGLDescriptorPool& _globalPool,
		BGLBindlessDescriptorManager& _descriptorManager)
		: bglDevice{ _bglDevice },
		globalPool{ _globalPool },
		descriptorManager{ _descriptorManager }
	{
		createSharedSampler();
	}

	BGLTextureLoader::~BGLTextureLoader()
	{
		// The shared sampler is owned here (content-texture packages reference it but no longer
		// destroy it — see BGLBindlessDescriptorManager).
		if (sharedSampler != VK_NULL_HANDLE)
			vkDestroySampler(BGLDevice::device(), sharedSampler, nullptr);
	}

	uint32_t BGLTextureLoader::loadTexture(const char* filePath, VkFormat imageFormat)
	{
		return buildAndStore(filePath, imageFormat);
	}

	uint32_t BGLTextureLoader::buildAndStore(const char* filePath, VkFormat imageFormat)
	{
		uint32_t storedIndex = descriptorManager.searchTextureName(filePath);
		bool designatedIndex = false;
		if (storedIndex != std::numeric_limits<uint32_t>::max())
		{
			char buff[256];
			if (!descriptorManager.checkMissingTexture(storedIndex))
			{
				sprintf(buff, "Texture %s is already bound", filePath);
				CONSOLE->Log("BGLTextureLoader", buff);
				return storedIndex;
			}
			sprintf(buff, "Texture %s is bound but marked missing — reloading", filePath);
			CONSOLE->Log("BGLTextureLoader", buff);
			designatedIndex = true;
		}

		const char* filetype = strrchr(filePath, '.');
		if (strcmp(filetype, ".ktx") == 0) {
			loadKTXImageInStagingBuffer(util::enginePath(filePath).c_str(), imageFormat);
		} else {
			loadSTBImageInStagingBuffer(util::enginePath(filePath).c_str(), imageFormat);
		}

		return buildAndStoreFromMemory(filePath, imageFormat, designatedIndex, storedIndex);
	}

	uint32_t BGLTextureLoader::buildAndStoreFromMemory(const char* name, VkFormat imageFormat, bool designatedIndex, uint32_t storedIndex)
	{
		VkImageView imageView;
		VkImage image;
		VkDeviceMemory memory;

		// For generated (stb/pixel) sources, decide the real mip count now that the format is
		// known: only if the GPU can linear-blit this format (otherwise stay at 1 mip). KTX
		// already set mipLvl from the file and leaves genMipchain false.
		if (genMipchain)
			mipLvl = formatSupportsLinearBlit(imageFormat) ? computeMipLevels(width, height) : 1;
		const bool willGenerate = genMipchain && mipLvl > 1;

		generateImageCreateInfo(imageFormat);
		bglDevice.createImageWithInfo(imageCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);
		generateSubresRange();      // covers all mipLvl levels
		setImageLayoutTransfer(image);

		VkCommandBuffer cpyCmd = bglDevice.beginSingleTimeCommands();
		// All levels: UNDEFINED -> TRANSFER_DST so we can upload/blit into them.
		vkCmdPipelineBarrier(cpyCmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemBarrier);
		// Upload the supplied levels. stb/pixel: just level 0. KTX: every level (buffCpyRegions).
		vkCmdCopyBufferToImage(cpyCmd, stagingBuffer->getBuffer(), image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(buffCpyRegions.size()), buffCpyRegions.data());

		if (willGenerate) {
			// Downsample level 0 into the rest of the chain; leaves all levels SHADER_READ_ONLY.
			generateMipmaps(cpyCmd, image, static_cast<int32_t>(width), static_cast<int32_t>(height), mipLvl);
		} else {
			// All uploaded levels: TRANSFER_DST -> SHADER_READ_ONLY in one barrier.
			setImageLayoutShaderRead();
			vkCmdPipelineBarrier(cpyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemBarrier);
		}
		bglDevice.endSingleTimeCommands(cpyCmd);

		// All content textures share one sampler (created once); the image view's levelCount
		// bounds the mip range for this specific texture.
		generateImageViewCreateInfo(imageFormat, image);
		VK_CHECK(vkCreateImageView(BGLDevice::device(), &imageViewCreateInfo, nullptr, &imageView));

		uint32_t handle = descriptorManager.storeTexture(
			{ sharedSampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			memory, image, name, designatedIndex, storedIndex);

		// Remember the handle so setMipLodBias() can rebind it to a retuned shared sampler.
		if (std::find(contentTextureHandles.begin(), contentTextureHandles.end(), handle) == contentTextureHandles.end())
			contentTextureHandles.push_back(handle);

		delete stagingBuffer;
		stagingBuffer = nullptr;
		buffCpyRegions.clear();
		genMipchain = false;        // reset for the next load
		return handle;
	}

	uint32_t BGLTextureLoader::loadTextureFromMemory(const char* name, const uint8_t* rgba, uint32_t w, uint32_t h, VkFormat imageFormat)
	{
		uint32_t storedIndex = descriptorManager.searchTextureName(name);
		bool designatedIndex = false;
		if (storedIndex != std::numeric_limits<uint32_t>::max())
		{
			if (!descriptorManager.checkMissingTexture(storedIndex)) return storedIndex;
			designatedIndex = true;
		}
		loadPixelDataInStagingBuffer(rgba, w, h, bytesPerTexel(imageFormat));
		return buildAndStoreFromMemory(name, imageFormat, designatedIndex, storedIndex);
	}

	uint32_t BGLTextureLoader::updateTextureFromMemory(const char* name, const uint8_t* rgba, uint32_t w, uint32_t h, VkFormat imageFormat)
	{
		uint32_t storedIndex = descriptorManager.searchTextureName(name);
		if (storedIndex == std::numeric_limits<uint32_t>::max())
			return loadTextureFromMemory(name, rgba, w, h, imageFormat); // not created yet -> first upload

		// Overwriting the designated slot destroys the old image/view; make sure no in-flight
		// frame still references it. (Demo-grade, matches the planet mesh rebuild's wait.)
		vkDeviceWaitIdle(BGLDevice::device());
		loadPixelDataInStagingBuffer(rgba, w, h, bytesPerTexel(imageFormat));
		return buildAndStoreFromMemory(name, imageFormat, /*designatedIndex*/ true, storedIndex);
	}

	BGLTextureLoader::HostVisibleImage BGLTextureLoader::createHostVisibleTexture(const char* name, uint32_t w, uint32_t h, VkFormat imageFormat)
	{
		HostVisibleImage result{};

		// Linear-tiled host-visible images can only be sampled if the format advertises it.
		VkFormatProperties fp{};
		vkGetPhysicalDeviceFormatProperties(bglDevice.getPhysicalDevice(), imageFormat, &fp);
		if (!(fp.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
			return result; // ok == false -> caller falls back to the staged (recreate) path

		// Overwrite in place if this name already has a slot (e.g. map reload). storeTexture frees
		// the old image/view; wait for idle first so no in-flight frame still references it.
		uint32_t storedIndex = descriptorManager.searchTextureName(name);
		bool designated = (storedIndex != std::numeric_limits<uint32_t>::max());
		if (designated) vkDeviceWaitIdle(BGLDevice::device());

		VkImageCreateInfo ici{};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = imageFormat;
		ici.extent = { w, h, 1 };
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_LINEAR;          // host-writable, sampled in place
		ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImage image;
		VkDeviceMemory memory;
		bglDevice.createImageWithInfo(ici, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, image, memory);

		// Row pitch of the single mip/layer; the CPU must honor it (it may exceed w*bpp).
		VkImageSubresource sub{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
		VkSubresourceLayout sl{};
		vkGetImageSubresourceLayout(BGLDevice::device(), image, &sub, &sl);

		void* mapped = nullptr;
		vkMapMemory(BGLDevice::device(), memory, 0, VK_WHOLE_SIZE, 0, &mapped);

		// UNDEFINED -> GENERAL once; GENERAL allows both host writes and shader sampling, so no
		// further transitions are ever needed (the CPU just writes the mapped memory each edit).
		VkCommandBuffer cmd = bglDevice.beginSingleTimeCommands();
		VkImageMemoryBarrier b{};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		b.srcAccessMask = 0;
		b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &b);
		bglDevice.endSingleTimeCommands(cmd);

		mipLvl = 1;                                      // single mip for the linear image
		generateImageViewCreateInfo(imageFormat, image); // builds imageViewCreateInfo with levelCount = mipLvl
		VkImageView imageView;
		VK_CHECK(vkCreateImageView(BGLDevice::device(), &imageViewCreateInfo, nullptr, &imageView));

		uint32_t handle = descriptorManager.storeTexture(
			{ sharedSampler, imageView, VK_IMAGE_LAYOUT_GENERAL },
			memory, image, name, designated, designated ? storedIndex : 0);
		if (std::find(contentTextureHandles.begin(), contentTextureHandles.end(), handle) == contentTextureHandles.end())
			contentTextureHandles.push_back(handle);

		result.handle = handle;
		result.mapped = static_cast<uint8_t*>(mapped) + sl.offset;
		result.rowPitch = sl.rowPitch;
		result.ok = true;
		return result;
	}

	uint32_t BGLTextureLoader::loadCombinedMetalRough(const char* roughPath, const char* metalPath)
	{
		std::string name = std::string("__mr_") + roughPath + "_" + metalPath;

		uint32_t storedIndex = descriptorManager.searchTextureName(name.c_str());
		bool designatedIndex = false;
		if (storedIndex != std::numeric_limits<uint32_t>::max())
		{
			if (!descriptorManager.checkMissingTexture(storedIndex)) return storedIndex;
			designatedIndex = true;
		}

		std::string fullRough = util::enginePath(roughPath);
		std::string fullMetal = util::enginePath(metalPath);

		int rw, rh, rc, mw, mh, mc;
		// Force single channel — we only need the gray value from each map
		unsigned char* roughPix = stbi_load(fullRough.c_str(), &rw, &rh, &rc, 1);
		unsigned char* metalPix = stbi_load(fullMetal.c_str(), &mw, &mh, &mc, 1);

		if (!roughPix || !metalPix)
		{
			std::cerr << "[BGLTextureLoader] failed to combine metalrough: "
			          << roughPath << " + " << metalPath << "\n";
			if (roughPix) stbi_image_free(roughPix);
			if (metalPix) stbi_image_free(metalPix);
			return 0;
		}

		// Use the smaller of the two dimensions if they differ
		uint32_t w = static_cast<uint32_t>(std::min(rw, mw));
		uint32_t h = static_cast<uint32_t>(std::min(rh, mh));

		std::vector<uint8_t> combined(w * h * 4);
		for (uint32_t i = 0; i < w * h; i++)
		{
			combined[i*4 + 0] = 255;          // R: AO (not available, default full)
			combined[i*4 + 1] = roughPix[i];  // G: roughness
			combined[i*4 + 2] = metalPix[i];  // B: metallic
			combined[i*4 + 3] = 255;          // A: unused
		}

		stbi_image_free(roughPix);
		stbi_image_free(metalPix);

		loadPixelDataInStagingBuffer(combined.data(), w, h);
		return buildAndStoreFromMemory(name.c_str(), VK_FORMAT_R8G8B8A8_UNORM, designatedIndex, storedIndex);
	}

	// Bytes per texel for an uncompressed color format. Lets the upload size match the format
	// (loadTextureFromMemory works for any format, not just RGBA8). Covers the formats the engine
	// uploads; asserts and falls back to 4 for anything unlisted.
	uint32_t BGLTextureLoader::bytesPerTexel(VkFormat format)
	{
		switch (format) {
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SRGB:
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SINT:
			return 1;
		case VK_FORMAT_R8G8_UNORM:
		case VK_FORMAT_R8G8_SRGB:
		case VK_FORMAT_R8G8_UINT:
		case VK_FORMAT_R16_UNORM:
		case VK_FORMAT_R16_SFLOAT:
			return 2;
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R16G16_SFLOAT:
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
			return 4;
		case VK_FORMAT_R16G16B16A16_UNORM:
		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R32G32_SFLOAT:
			return 8;
		case VK_FORMAT_R32G32B32A32_SFLOAT:
			return 16;
		default:
			assert(false && "bytesPerTexel: unhandled VkFormat — add it here");
			return 4;
		}
	}

	void BGLTextureLoader::loadPixelDataInStagingBuffer(const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t bpp)
	{
		width     = w;
		height    = h;
		mipLvl    = 1;          // only level 0 is provided; the rest is generated on the GPU
		genMipchain = true;     // pre-decoded source -> generate the mip chain via blits
		imageSize = static_cast<size_t>(w) * h * bpp;

		stagingBuffer = new BGLBuffer(
			bglDevice, 1, static_cast<uint32_t>(imageSize),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		stagingBuffer->map();
		stagingBuffer->writeToBuffer(const_cast<uint8_t*>(rgba));
		populateBufferCopyRegionSTB(buffCpyRegions, width, height);
	}

	void BGLTextureLoader::loadSTBImageInStagingBuffer(const char* filePath, VkFormat format)
	{
		int width_int; int height_int; int channels;
		stbi_info(filePath, &width_int, &height_int, &channels);
		//std::cout << filePath << " is " << width_int << "x" << height_int << " and has " << channels << " channels\n";
		unsigned char* pixels = stbi_load(filePath, &width_int, &height_int, &channels, 0);
		if (!pixels) {
			std::cout
				<< "Unable to load image: "
				<< stbi_failure_reason()
				<< "\n";
			return;
		}
		width = width_int;
		height = height_int;
		mipLvl = 1;             // only level 0 is decoded here; the rest is generated on the GPU
		genMipchain = true;     // stb source -> generate the mip chain via blits
		imageSize = width * height * 4;

		unsigned char* modified = new unsigned char[imageSize];
		const unsigned char alpha = 255;

		for (size_t i = 0, j = 0; i < imageSize; i += 4, j += channels) {
			for (uint8_t ii = 0; ii < channels; ii++) {
				modified[i+ii] = pixels[j + ii];     // channels
			}
			for (uint8_t iii = 0; iii < 4-channels; iii++) {
				modified[i + channels + iii] = alpha;     // extra channels
			}          
		}

		stagingBuffer = new BGLBuffer(
			bglDevice,
			1,
			static_cast<uint32_t>(imageSize),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		stagingBuffer->map();
		stagingBuffer->writeToBuffer(modified);
		populateBufferCopyRegionSTB(buffCpyRegions, width, height);

		delete pixels;
	}

	void BGLTextureLoader::loadKTXImageInStagingBuffer(const char* filePath, VkFormat format)
	{
		ktxTexture* ktx_texture;
		KTX_error_code result;
		result = ktxTexture_CreateFromNamedFile(filePath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);
		std::string errlog = "Error loading texture: " + std::string(filePath);
		assert(ktx_texture != nullptr && errlog.c_str());

		width = ktx_texture->baseWidth;
		height = ktx_texture->baseHeight;
		mipLvl = ktx_texture->numLevels;
		genMipchain = false;    // KTX ships its full mip pyramid; upload it as-is, don't generate

		ktx_uint8_t* ktxImageData = ktx_texture->pData;
		imageSize = ktx_texture->dataSize;
		/*for (int i = 0; i < mipLvl; i++) {
			ktx_size_t offset;
			KTX_error_code    result = ktxTexture_GetImageOffset(ktx_texture, i, 0, 0, &offset);
		}*/

		stagingBuffer = new BGLBuffer(
			bglDevice,
			1,
			static_cast<uint32_t>(imageSize),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		stagingBuffer->map();
		stagingBuffer->writeToBuffer(ktxImageData);
		populateBufferCopyRegionKTX(buffCpyRegions, ktx_texture, mipLvl);

		// raw data no longer needed
		ktxTexture_Destroy(ktx_texture);
	}

	void BGLTextureLoader::generateSubresRange()
	{
		subresRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresRange.baseMipLevel = 0;
		subresRange.levelCount = mipLvl;
		subresRange.layerCount = 1;
	}

	void BGLTextureLoader::setImageLayoutTransfer(VkImage image)
	{
		imageMemBarrier.pNext = nullptr;
		imageMemBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemBarrier.image = image;
		imageMemBarrier.subresourceRange = subresRange;
		imageMemBarrier.srcAccessMask = 0;
		imageMemBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}

	void BGLTextureLoader::setImageLayoutShaderRead()
	{
		assert(imageMemBarrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && "imageMemBarrier should be initialized with BGLBGLTextureLoader::setImageLayoutTransfer()");
		imageMemBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	void BGLTextureLoader::generateImageCreateInfo(VkFormat imageFormat)
	{
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = imageFormat;
		imageCreateInfo.mipLevels = mipLvl;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width, height, 1 };
		// TRANSFER_SRC is required so each mip level can be the source of the blit that
		// downsamples it into the next level (see generateMipmaps). Harmless when not generating.
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	void BGLTextureLoader::generateSamplerCreateInfo()
	{
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerCreateInfo.minLod = 0.0f;
		samplerCreateInfo.maxLod = static_cast<float>(mipLvl);
		if (bglDevice.supportedFeatures.samplerAnisotropy)
		{
			samplerCreateInfo.maxAnisotropy = bglDevice.properties.limits.maxSamplerAnisotropy;
			samplerCreateInfo.anisotropyEnable = VK_TRUE;
		}
		else
		{
			// The device does not support anisotropic filtering
			samplerCreateInfo.maxAnisotropy = 1.0;
			samplerCreateInfo.anisotropyEnable = VK_FALSE;
		}
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	}
	// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
	// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
	void BGLTextureLoader::generateImageViewCreateInfo(VkFormat imageFormat, VkImage image)
	{
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = imageFormat;
		imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		// Linear tiling usually won't support mip maps
		// Only set mip map count if optimal tiling is used
		imageViewCreateInfo.subresourceRange.levelCount = mipLvl;
		imageViewCreateInfo.image = image; // The view will be based on the texture's image
	}

	uint32_t BGLTextureLoader::computeMipLevels(uint32_t w, uint32_t h)
	{
		// floor(log2(max(w,h))) + 1, computed with shifts (no <cmath>): each halving of the
		// largest dimension is one more level, down to the 1x1 tail.
		uint32_t levels = 1;
		uint32_t d = std::max(w, h);
		while (d > 1) { d >>= 1; ++levels; }
		return levels;
	}

	bool BGLTextureLoader::formatSupportsLinearBlit(VkFormat format) const
	{
		// vkCmdBlitImage with VK_FILTER_LINEAR requires the format to advertise
		// SAMPLED_IMAGE_FILTER_LINEAR on the (optimal-tiled) image we sample from. R8G8B8A8
		// UNORM/SRGB support it on all desktop GPUs; the guard keeps exotic formats safe.
		VkFormatProperties props{};
		vkGetPhysicalDeviceFormatProperties(bglDevice.getPhysicalDevice(), format, &props);
		return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
	}

	void BGLTextureLoader::generateMipmaps(VkCommandBuffer cmd, VkImage image, int32_t w, int32_t h, uint32_t mipLevels)
	{
		// One reusable single-level barrier; baseMipLevel is moved as we walk the chain.
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipW = w, mipH = h;
		for (uint32_t i = 1; i < mipLevels; i++)
		{
			// Flip level i-1 from "just written" (TRANSFER_DST) to a readable blit source.
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			// Halve each dimension (min 1 -> handles non-power-of-two). LINEAR filter does the
			// 2x2 box downsample. NOTE: for sRGB formats this filters in sRGB space, so color
			// mips come out slightly dark — acceptable; UNORM (normal/ORM) maps are exact.
			const int32_t nextW = mipW > 1 ? mipW / 2 : 1;
			const int32_t nextH = mipH > 1 ? mipH / 2 : 1;
			VkImageBlit blit{};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipW, mipH, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { nextW, nextH, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;
			vkCmdBlitImage(cmd,
				image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit, VK_FILTER_LINEAR);

			// Level i-1 is fully produced -> make it shader-readable now.
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			mipW = nextW;
			mipH = nextH;
		}

		// The last level was never a blit source, so it's still TRANSFER_DST -> transition it.
		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	void BGLTextureLoader::createSharedSampler()
	{
		// One sampler for all content textures. The per-texture image VIEW bounds the mip
		// range, so maxLod is left unbounded (VK_LOD_CLAMP_NONE) here.
		VkSamplerCreateInfo s{};
		s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		s.magFilter = VK_FILTER_LINEAR;
		s.minFilter = VK_FILTER_LINEAR;
		s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; // trilinear: blend between mips, no seam
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		s.mipLodBias = mipLodBias;                    // the direct "mip distance" dial
		s.compareOp = VK_COMPARE_OP_NEVER;
		s.minLod = 0.0f;
		s.maxLod = VK_LOD_CLAMP_NONE;                 // use every level the image view exposes
		// Anisotropy: the biggest quality lever for oblique surfaces (floors/walls at grazing
		// angles) — without it they blur far too early.
		if (bglDevice.supportedFeatures.samplerAnisotropy) {
			s.maxAnisotropy = bglDevice.properties.limits.maxSamplerAnisotropy;
			s.anisotropyEnable = VK_TRUE;
		} else {
			s.maxAnisotropy = 1.0f;
			s.anisotropyEnable = VK_FALSE;
		}
		s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK(vkCreateSampler(BGLDevice::device(), &s, nullptr, &sharedSampler));
	}

	void BGLTextureLoader::setMipLodBias(float bias)
	{
		// Samplers are immutable, so retuning means: build a new one, re-point every content
		// texture's descriptor at it, then retire the old one. GPU must be idle first since the
		// old sampler may still be referenced by in-flight frames.
		mipLodBias = bias;
		vkDeviceWaitIdle(BGLDevice::device());

		VkSampler old = sharedSampler;
		createSharedSampler(); // overwrites sharedSampler with the retuned one
		for (uint32_t handle : contentTextureHandles)
			descriptorManager.rebindTextureSampler(handle, sharedSampler);

		if (old != VK_NULL_HANDLE)
			vkDestroySampler(BGLDevice::device(), old, nullptr);
	}

}
