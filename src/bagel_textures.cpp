#include "bagel_textures.hpp"
#include "bagel_engine_swap_chain.hpp"
#include "bagel_util.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include <cassert>
#include <iostream>
#include <array>
#include <limits>

#define GLOBAL_DESCRIPTOR_COUNT 1000

#define VK_CHECK(x)                                                     \
	do                                                                  \
	{                                                                   \
		VkResult err = x;                                               \
		if (err)                                                        \
		{                                                               \
			std::cout <<"Detected Vulkan error: " << err << std::endl;  \
			abort();                                                    \
		}                                                               \
	} while (0)

//https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/api/texture_loading/texture_loading.cpp
namespace bagel {
	//Working example of texture loading
	bool load_image_from_file(BGLDevice& bglDevice, const char* file, BGLTexture::BGLTextureInfoComponent& texture)
	{
		throw("load_image_from_file() will be deprecated");
		// We use the Khronos texture format (https://www.khronos.org/opengles/sdk/tools/KTX/file_format_spec/)
		// ktx1 doesn't know whether the content is sRGB or linear, but most tools save in sRGB, so assume that.
		VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

		ktxTexture* ktx_texture;
		KTX_error_code result;
		result = ktxTexture_CreateFromNamedFile(file, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);

		if (ktx_texture == nullptr)
		{
			throw std::runtime_error("Couldn't load texture");
		}
		
		texture.width = ktx_texture->baseWidth;
		texture.height = ktx_texture->baseHeight;
		texture.mip_levels = ktx_texture->numLevels;
		//std::cout << " image has " << texture.mip_levels << " mipmaps\n";
		//std::cout << " image WxH " << texture.width << "x" << texture.height << "\n";
		// We prefer using staging to copy the texture data to a device local optimal image
		VkBool32 use_staging = true;

		ktx_uint8_t* ktx_image_data = ktx_texture->pData;
		ktx_size_t   ktx_texture_size = ktx_texture->dataSize;

		// Copy data to an optimal tiled image
		// This loads the texture data into a host local buffer that is copied to the optimal tiled image on the device

		// Create a host-visible staging buffer that contains the raw image data
		// This buffer will be the data source for copying texture data to the optimal tiled image on the device
		VkBuffer       staging_buffer;
		VkDeviceMemory staging_memory;

		VkBufferCreateInfo buffer_create_info{};
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = ktx_texture_size;
		//std::cout << "Allocating " << ktx_texture_size << " bytes of memory\n";
		// This buffer is used as a transfer source for the buffer copy
		buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK(vkCreateBuffer(BGLDevice::device(), &buffer_create_info, nullptr, &staging_buffer));

		// Get memory requirements for the staging buffer (alignment, memory type bits)
		VkMemoryRequirements staging_buffer_memory_requirements{};
		vkGetBufferMemoryRequirements(BGLDevice::device(), staging_buffer, &staging_buffer_memory_requirements);
		VkMemoryAllocateInfo staging_buffer_memory_allocate_info{};
		staging_buffer_memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		staging_buffer_memory_allocate_info.allocationSize = staging_buffer_memory_requirements.size;

		//std::cout << "memory_allocate_info.size = " << memory_requirements.size << "\n";
		VkMemoryPropertyFlags stagingBufferFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		// Get memory type index for a host visible buffer
		staging_buffer_memory_allocate_info.memoryTypeIndex = bglDevice.findMemoryType(staging_buffer_memory_requirements.memoryTypeBits, stagingBufferFlags);
		VK_CHECK(vkAllocateMemory(BGLDevice::device(), &staging_buffer_memory_allocate_info, nullptr, &staging_memory));
		VK_CHECK(vkBindBufferMemory(BGLDevice::device(), staging_buffer, staging_memory, 0));

		// Copy texture data into host local staging buffer

		uint8_t* data;
		VK_CHECK(vkMapMemory(BGLDevice::device(), staging_memory, 0, staging_buffer_memory_requirements.size, 0, (void**)&data));
		memcpy(data, ktx_image_data, ktx_texture_size);
		vkUnmapMemory(BGLDevice::device(), staging_memory);


		// Setup buffer copy regions for each mip level
		std::vector<VkBufferImageCopy> buffer_copy_regions;
		for (uint32_t i = 0; i < texture.mip_levels; i++)
		{
			ktx_size_t        offset;
			KTX_error_code    result = ktxTexture_GetImageOffset(ktx_texture, i, 0, 0, &offset);
			VkBufferImageCopy buffer_copy_region = {};
			buffer_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			buffer_copy_region.imageSubresource.mipLevel = i;
			buffer_copy_region.imageSubresource.baseArrayLayer = 0;
			buffer_copy_region.imageSubresource.layerCount = 1;
			buffer_copy_region.imageExtent.width = ktx_texture->baseWidth >> i;
			buffer_copy_region.imageExtent.height = ktx_texture->baseHeight >> i;
			buffer_copy_region.imageExtent.depth = 1;
		
			buffer_copy_region.bufferOffset = offset;
			//std::cout << "mipmap level: " << i << " offset: " << offset << " WxH: " << buffer_copy_region.imageExtent.width << "x" << buffer_copy_region.imageExtent.height << "\n";
			buffer_copy_regions.push_back(buffer_copy_region);
		}

		// Create optimal tiled target image on the device
		VkImageCreateInfo image_create_info{};
		image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_create_info.imageType = VK_IMAGE_TYPE_2D;
		image_create_info.format = format;
		image_create_info.mipLevels = texture.mip_levels;
		image_create_info.arrayLayers = 1;
		image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		// Set initial layout of the image to undefined
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_create_info.extent = { texture.width, texture.height, 1 };
		image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK(vkCreateImage(BGLDevice::device(), &image_create_info, nullptr, &texture.image));

		VkMemoryRequirements image_memory_requirements{};
		vkGetImageMemoryRequirements(BGLDevice::device(), texture.image, &image_memory_requirements);
		VkMemoryAllocateInfo image_memory_allocate_info{};
		image_memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		image_memory_allocate_info.allocationSize = image_memory_requirements.size;
		image_memory_allocate_info.memoryTypeIndex = bglDevice.findMemoryType(image_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK(vkAllocateMemory(BGLDevice::device(), &image_memory_allocate_info, nullptr, &texture.device_memory));
		VK_CHECK(vkBindImageMemory(BGLDevice::device(), texture.image, texture.device_memory, 0));

		//Create copy_command buffer and start recording
		VkCommandBufferAllocateInfo cmd_buf_allocate_info{};
		cmd_buf_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmd_buf_allocate_info.commandPool = bglDevice.getCommandPool();
		cmd_buf_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd_buf_allocate_info.commandBufferCount = 1;

		VkCommandBuffer copy_command;
		VK_CHECK(vkAllocateCommandBuffers(BGLDevice::device(), &cmd_buf_allocate_info, &copy_command));

		// If requested, also start recording for the new command buffer
		VkCommandBufferBeginInfo command_buffer_info{};
		command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VK_CHECK(vkBeginCommandBuffer(copy_command, &command_buffer_info));

		// Image memory barriers for the texture image

		// The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
		VkImageSubresourceRange subresource_range = {};
		// Image only contains color data
		subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		// Start at first mip level
		subresource_range.baseMipLevel = 0;
		// We will transition on all mip levels
		subresource_range.levelCount = texture.mip_levels;
		// The 2D texture only has one layer
		subresource_range.layerCount = 1;

		// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
		VkImageMemoryBarrier image_memory_barrier{};
		image_memory_barrier.pNext = nullptr;
		image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_memory_barrier.image = texture.image;
		image_memory_barrier.subresourceRange = subresource_range;
		image_memory_barrier.srcAccessMask = 0;
		image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage is host write/read execution (VK_PIPELINE_STAGE_HOST_BIT)
		// Destination pipeline stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
		vkCmdPipelineBarrier(
			copy_command,
			VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &image_memory_barrier);
		// Copy mip levels from staging buffer
		vkCmdCopyBufferToImage(
			copy_command,
			staging_buffer,
			texture.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(buffer_copy_regions.size()),
			buffer_copy_regions.data());

		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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

		// Store current layout for later reuse
		texture.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//End recording command buffer and create fence to ensure that it finished executing
		VK_CHECK(vkEndCommandBuffer(copy_command));

		VkSubmitInfo submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &copy_command;

		//// Create fence to ensure that the command buffer has finished executing
		//VkFenceCreateInfo fence_info{};
		//fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		//fence_info.flags = VK_FLAGS_NONE;

		//VkFence fence;
		//VK_CHECK(vkCreateFence(bglDevice.device(), &fence_info, nullptr, &fence));

		//// Submit to the queue
		//VkResult result = vkQueueSubmit(queue, 1, &submit_info, fence);
		//// Wait for the fence to signal that command buffer has finished executing
		//VK_CHECK(vkWaitForFences(bglDevice.device(), 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

		//vkDestroyFence(bglDevice.device(), fence, nullptr);

		//if (command_pool && free)
		//{
		//	vkFreeCommandBuffers(handle, command_pool->get_handle(), 1, &command_buffer);
		//}

	// Clean up staging resources
		vkDestroyBuffer(BGLDevice::device(), staging_buffer, nullptr);
		vkFreeMemory(BGLDevice::device(), staging_memory, nullptr);
	
		return true;
	}
	
	BGLTexture::BGLTexture(BGLDevice& device, VkFormat format) : bglDevice{ device }, imageFormat{ format } {}

	BGLTexture::~BGLTexture()
	{
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
		populateBufferCopyRegion(texture->buffer_copy_regions, ktx_texture, texture->info.mip_levels);

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

	void populateBufferCopyRegion(std::vector<VkBufferImageCopy> &buffer_copy_regions, ktxTexture* ktx_texture, uint32_t mip_levels)
	{
		// Setup buffer copy regions for each mip level
		for (uint32_t i = 0; i < mip_levels; i++)
		{
			std::cout << "Mipmap index: " << i << "\n";
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

	TextureComponentBuilder::TextureComponentBuilder(
		BGLDevice& _bglDevice,
		BGLDescriptorPool& _globalPool,
		BGLBindlessDescriptorManager& _descriptorManager)
		: bglDevice{ _bglDevice },
		globalPool{ _globalPool },
		descriptorManager{ _descriptorManager }
	{}

	TextureComponentBuilder::~TextureComponentBuilder()
	{}

	void TextureComponentBuilder::buildComponent(std::string filePath, VkFormat imageFormat)
	{
		buildComponent(filePath.c_str(), imageFormat);
	}

	// filePath relative to engine path starting with /
	void TextureComponentBuilder::buildComponent(const char* filePath, VkFormat imageFormat)
	{
#define memorySave
#ifdef memorySaveOld
		std::string filenameStr(filePath);
		if (lastBoundTextureName == filenameStr)
		{
			//Don't bind again. Just use the last bound texturehandle
			targetComponent->textureHandle = descriptorManager.getLastTextureHandle();
			lastBoundTextureName = filenameStr;
			std::cout << filenameStr << " same as the last bound texture. Skipping memory allocation\n";
			targetComponent->duplicate = true;
			return;
		}
		lastBoundTextureName = filenameStr;
#else
		uint32_t storedIndex = descriptorManager.searchTextureName(filePath);
		if (storedIndex != std::numeric_limits<uint32_t>::max())
		{
			std::cout << filePath << " Texture already bound\n";
			targetComponent->textureHandle = storedIndex;
			return;
		}
#endif
		assert(targetComponent != nullptr && "No targetComponent set for TextureComponentBuilder");
		// Staging Buffer is created here
		loadKTXImageInStagingBuffer(util::enginePath(filePath).c_str(), imageFormat);
		generateImageCreateInfo(imageFormat);
		//VK_CHECK(vkCreateImage(bglDevice.device(), &imageCreateInfo, nullptr, &targetComponent.image));
		bglDevice.createImageWithInfo(imageCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, targetComponent->image, targetComponent->device_memory);
		generateSubresRange();
		setImageLayoutTransfer(targetComponent->image);

		VkCommandBuffer cpyCmd = bglDevice.beginSingleTimeCommands();

		vkCmdPipelineBarrier(cpyCmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0, nullptr,0, nullptr,1, &imageMemBarrier);
		vkCmdCopyBufferToImage(cpyCmd, stagingBuffer->getBuffer(),targetComponent->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,static_cast<uint32_t>(buffCpyRegions.size()),buffCpyRegions.data());
		
		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		setImageLayoutShaderRead();
		vkCmdPipelineBarrier(cpyCmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0, nullptr,0, nullptr,1, &imageMemBarrier);
		
		bglDevice.endSingleTimeCommands(cpyCmd);

		targetComponent->image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		generateSamplerCreateInfo();
		VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerCreateInfo, nullptr, &targetComponent->sampler));

		generateImageViewCreateInfo(imageFormat, targetComponent->image);
		VK_CHECK(vkCreateImageView(BGLDevice::device(), &imageViewCreateInfo, nullptr, &targetComponent->view));
		
		targetComponent->textureHandle = descriptorManager.storeTexture(targetComponent->view, targetComponent->sampler, filePath);
		delete stagingBuffer;
		buffCpyRegions.clear();
	}

	void TextureComponentBuilder::loadKTXImageInStagingBuffer(const char* filePath, VkFormat format)
	{
		ktxTexture* ktx_texture;
		KTX_error_code result;
		result = ktxTexture_CreateFromNamedFile(filePath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);
		std::string errlog = "Error loading texture: " + std::string(filePath);
		assert(ktx_texture != nullptr && errlog.c_str());

		width = ktx_texture->baseWidth;
		height = ktx_texture->baseHeight;
		mipLvl = ktx_texture->numLevels;

		ktx_uint8_t* ktxImageData = ktx_texture->pData;
		ktxTextureSize = ktx_texture->dataSize;
		for (int i = 0; i < mipLvl; i++) {
			ktx_size_t offset;
			KTX_error_code    result = ktxTexture_GetImageOffset(ktx_texture, i, 0, 0, &offset);
		}

		stagingBuffer = new BGLBuffer(
			bglDevice,
			1,
			static_cast<uint32_t>(ktxTextureSize),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		stagingBuffer->map();
		stagingBuffer->writeToBuffer(ktxImageData);
		populateBufferCopyRegion(buffCpyRegions, ktx_texture, mipLvl);

		// raw data no longer needed
		ktxTexture_Destroy(ktx_texture);
	}

	void TextureComponentBuilder::generateSubresRange()
	{
		subresRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresRange.baseMipLevel = 0;
		subresRange.levelCount = mipLvl;
		subresRange.layerCount = 1;
	}

	void TextureComponentBuilder::setImageLayoutTransfer(VkImage image)
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

	void TextureComponentBuilder::setImageLayoutShaderRead()
	{
		assert(imageMemBarrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && "imageMemBarrier should be initialized with BGLTextureComponentBuilder::setImageLayoutTransfer()");
		imageMemBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	void TextureComponentBuilder::generateImageCreateInfo(VkFormat imageFormat)
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
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	void TextureComponentBuilder::generateSamplerCreateInfo()
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
	void TextureComponentBuilder::generateImageViewCreateInfo(VkFormat imageFormat, VkImage image)
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

}
