#include "bagel_renderer.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include <algorithm>
#include <stdexcept>
#include <array>
#include <iostream>

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
/// 
/// Implementing deferred rendering
/// 1. Create descriptor bindings
///     Create a completely new descriptor set just for the composition stage
///     Binding 1 : Position texture target / Scene colormap
///     Binding 2 : Normals texture target
///     Binding 3 : Albedo texture target
///     Binding 4 : Fragment shader uniform buffer
///     VkDescriptorImageInfo for each bindings
///     and write the descriptor
/// 2. Render models to deferred command buffer 
/// 3. main command buffer only draws with offscreen attachments
/// 
namespace bagel {
	BGLRenderer::BGLRenderer(BGLWindow& w, BGLDevice& d) : bglWindow{ w }, bglDevice{ d }
	{
		recreateSwapChain();
		createCommandBuffers();
		prepareDeferredRenderFrameBuffer();
		prepareBloomMips();
		gbufferExtent = bglSwapChain->getSwapChainExtent();
	}
	BGLRenderer::~BGLRenderer()
	{
		std::cout << "Destroying BGLRenderer\n";
		freeCommandBuffers();
		std::cout << "Finished Destroying BGLRenderer\n";
	}

	VkCommandBuffer BGLRenderer::beginPrimaryCMD()
	{
		assert(!isFrameStarted && "Can not call beginPrimaryCMD() while the frame is already started");
		auto result = bglSwapChain->acquireNextImage(&currentImageIndex);

		//detect if the surface is no longer compatible with the swapchain
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapChain();
			return nullptr;
		}

		if (result == VK_SUBOPTIMAL_KHR) {
			recreateSwapChain();
			return nullptr;
		}

		if (result != VK_SUCCESS) {
			throw std::runtime_error("failed to acquire swapchain image");
		}

		isFrameStarted = true;
		auto commandBuffer = getCurrentCommandBuffer();

		// Record draw commands to each command buffers
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to begin recording draw command to command buffers");
		}
		return commandBuffer;
	}

	void BGLRenderer::endPrimaryCMD()
	{
		assert(isFrameStarted && "Cannot call endFrame() while frame is not in progress");

		auto commandBuffer = getCurrentCommandBuffer();
		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
			throw std::runtime_error("Failed to record command buffer");
		}

		auto result = bglSwapChain->submitCommandBuffers(&commandBuffer, &currentImageIndex);
		//VK_SUBOPTIMAL_KHR means the swapchain no longer matches the surface but can be used to present on the surface
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || bglWindow.wasWindowResized()) 
		{
			bglWindow.resetWindowResizedFlag();
			recreateSwapChain();
		} else if (result != VK_SUCCESS) {
			throw std::runtime_error("failed to present swapchain image");
		}

		isFrameStarted = false;
		currentFrameIndex = (currentFrameIndex + 1) % BGLSwapChain::MAX_FRAMES_IN_FLIGHT;
	}

	void BGLRenderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted && "Cannot call beginSwapChainRenderPass() while frame is not in progress"); 
		assert(commandBuffer == getCurrentCommandBuffer() && "Cannot begin renderpass from a different frame");

		// Record draw commands to each command buffers
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

		renderPassInfo.renderPass = bglSwapChain->getRenderPass();
		renderPassInfo.framebuffer = bglSwapChain->getFrameBuffer(currentImageIndex);

		renderPassInfo.renderArea.offset = { 0,0 };
		// Make sure to use the swapchain extent not the window extent
		// because the swapchain extent may be larger then window extent which is the case in Mac retina display
		renderPassInfo.renderArea.extent = bglSwapChain->getSwapChainExtent();

		// Set the color that the frame buffer 'attachments' will clear to 
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { 0.01f, 0.01f, 0.01f, 1.0f }; // color attachment
		clearValues[1].depthStencil = { 1.0f, 0 }; // Depths stencil clear value

		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		// VK_SUBPASS_CONTENTS_INLINE indicate that no secondary command buffers are in use

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(bglSwapChain->getSwapChainExtent().width);
		viewport.height = static_cast<float>(bglSwapChain->getSwapChainExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{ {0,0}, bglSwapChain->getSwapChainExtent() };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	//vkCmdEndRenderPass
	void BGLRenderer::endCurrentRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted && "Cannot call endSwapChainRenderPass() while frame is not in progress");
		assert(commandBuffer == getCurrentCommandBuffer() && "Cannot end renderpass from a different frame");
		vkCmdEndRenderPass(commandBuffer);
	}

	void BGLRenderer::beginOffScreenRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted && "Cannot call beginOffScreenRenderPass() while frame is not in progress");
		assert(commandBuffer == getCurrentCommandBuffer() && "Cannot begin renderpass from a different frame");

		// Record draw commands to each command buffers
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

		renderPassInfo.renderPass = offscreenPass.renderPass;
		renderPassInfo.framebuffer = offscreenPass.frameBuffer;

		renderPassInfo.renderArea.offset = { 0,0 };
		// Make sure to use the swapchain extent not the window extent
		// because the swapchain extent may be larger then window extent which is the case in Mac retina display
		renderPassInfo.renderArea.extent = { offscreenPass.width, offscreenPass.height};

		// Set the color that the frame buffer 'attachments' will clear to 
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { 0.01f, 0.01f, 0.01f, 1.0f }; // color attachment
		clearValues[1].depthStencil = { 1.0f, 0 }; // Depths stencil clear value

		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		// VK_SUBPASS_CONTENTS_INLINE indicate that no secondary command buffers are in use

		VkViewport viewport{};

		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(offscreenPass.width);
		viewport.height = static_cast<float>(offscreenPass.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{ {0,0}, {offscreenPass.width, offscreenPass.height} };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}
	
	void BGLRenderer::recreateSwapChain()
	{
		auto extent = bglWindow.getExtent();
		//If there is at least one dimension with length of 0(or sizeless), the program will pause which is during minimization.
		while (extent.width == 0 || extent.height == 0) {
			extent = bglWindow.getExtent();
			glfwWaitEvents();
		}
		//Wait for current swapchain to no longer be used
		vkDeviceWaitIdle(BGLDevice::device());
		if (bglSwapChain == nullptr) {
			bglSwapChain = std::make_unique<BGLSwapChain>(bglDevice, extent);
		}
		else {
			std::shared_ptr<BGLSwapChain> oldSwapChain = std::move(bglSwapChain);
			bglSwapChain = std::make_unique<BGLSwapChain>(bglDevice, extent, oldSwapChain);

			if (!oldSwapChain->compareSwapFormat(*bglSwapChain.get())) {
				throw std::runtime_error("Swap chain image depth format has changed!");
			}

		}
		// Rebuild G-buffer and bloom mips if the swapchain extent changed
		auto newExtent = bglSwapChain->getSwapChainExtent();
		if (gbufferExtent.width != 0 &&
			(newExtent.width != gbufferExtent.width || newExtent.height != gbufferExtent.height)) {
			destroyDeferredFrameBuffer();
			destroyBloomMips();
			prepareDeferredRenderFrameBuffer();
			prepareBloomMips();
			gbufferExtent = newExtent;
			gbufferRecreated = true;
		}
	}

	void BGLRenderer::setUpOffScreenRenderPass(uint32_t textureWidth, uint32_t textureHeight)
	{
		createOffScreenRenderPass(textureWidth, textureHeight);
		createOffscreenFrameBuffer();
	}

	void BGLRenderer::createOffScreenRenderPass(uint32_t textureWidth, uint32_t textureHeight)
	{
		offscreenPass.width = textureWidth;
		offscreenPass.height = textureHeight;
		// Depth stencil attachment
		VkFormat fbDepthFormat;
		VkBool32 validDepthFormat = bglDevice.getSupportedDepthsFormat(&fbDepthFormat);
		assert(validDepthFormat);
		createOffscreenColorAttachment();
		createOffscreenDepthsAttachment(fbDepthFormat);

		std::array<VkAttachmentDescription, 2> attchmentDescriptions{};
		createOffscreenAttachmentDescriptors(attchmentDescriptions, fbDepthFormat);

		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;

		std::array<VkSubpassDependency, 2> dependencies{};
		createOffscreenSubpassDependencies(dependencies);
		
		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
		renderPassInfo.pAttachments = attchmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &renderPassInfo, nullptr, &offscreenPass.renderPass));
		
	}

	void BGLRenderer::createOffscreenColorAttachment()
	{
		VkImageCreateInfo image{};
		image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
		image.extent.width = offscreenPass.width;
		image.extent.height = offscreenPass.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc{};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		VkMemoryRequirements memReqs;

		VK_CHECK(vkCreateImage(BGLDevice::device(), &image, nullptr, &offscreenPass.color.image));
		vkGetImageMemoryRequirements(BGLDevice::device(), offscreenPass.color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = bglDevice.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VK_CHECK(vkAllocateMemory(BGLDevice::device(), &memAlloc, nullptr, &offscreenPass.color.mem));
		VK_CHECK(vkBindImageMemory(BGLDevice::device(), offscreenPass.color.image, offscreenPass.color.mem, 0));

		VkImageViewCreateInfo colorImageView{};
		colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = offscreenPass.color.image;
		VK_CHECK(vkCreateImageView(BGLDevice::device(), &colorImageView, nullptr, &offscreenPass.color.view));

		// Create sampler to sample from the attachment in the fragment shader
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = samplerInfo.addressModeU;
		samplerInfo.addressModeW = samplerInfo.addressModeU;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &offscreenPass.sampler));
	}

	void BGLRenderer::createOffscreenDepthsAttachment(VkFormat& depthsFormat)
	{
		VkImageCreateInfo image{};
		image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = depthsFormat;
		image.extent.width = offscreenPass.width;
		image.extent.height = offscreenPass.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		VkMemoryAllocateInfo memAlloc{};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		VkMemoryRequirements memReqs;

		VK_CHECK(vkCreateImage(BGLDevice::device(), &image, nullptr, &offscreenPass.depth.image));
		vkGetImageMemoryRequirements(BGLDevice::device(), offscreenPass.depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = bglDevice.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK(vkAllocateMemory(BGLDevice::device(), &memAlloc, nullptr, &offscreenPass.depth.mem));
		VK_CHECK(vkBindImageMemory(BGLDevice::device(), offscreenPass.depth.image, offscreenPass.depth.mem, 0));

		VkImageViewCreateInfo depthStencilView{};
		depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = depthsFormat;
		depthStencilView.flags = 0;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (depthsFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
			depthStencilView.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;
		depthStencilView.image = offscreenPass.depth.image;
		VK_CHECK(vkCreateImageView(BGLDevice::device(), &depthStencilView, nullptr, &offscreenPass.depth.view));
	}

	void BGLRenderer::createOffscreenAttachmentDescriptors(std::array<VkAttachmentDescription, 2>& descriptors, VkFormat& depthsFormat)
	{
		// Color attachment
		descriptors[0].format = VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
		descriptors[0].samples = VK_SAMPLE_COUNT_1_BIT;
		descriptors[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		descriptors[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		descriptors[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		descriptors[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		descriptors[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		descriptors[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// Depth attachment
		descriptors[1].format = depthsFormat;
		descriptors[1].samples = VK_SAMPLE_COUNT_1_BIT;
		descriptors[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		descriptors[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		descriptors[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		descriptors[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		descriptors[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		descriptors[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	}

	void BGLRenderer::createOffscreenSubpassDependencies(std::array<VkSubpassDependency, 2>& dependencies)
	{
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	}

	void BGLRenderer::createOffscreenFrameBuffer()
	{
		VkImageView attachments[2];
		attachments[0] = offscreenPass.color.view;
		attachments[1] = offscreenPass.depth.view;

		VkFramebufferCreateInfo fbufCreateInfo{};
		fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbufCreateInfo.renderPass = offscreenPass.renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = offscreenPass.width;
		fbufCreateInfo.height = offscreenPass.height;
		fbufCreateInfo.layers = 1;

		VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbufCreateInfo, nullptr, &offscreenPass.frameBuffer));
	}

	void BGLRenderer::beginDeferredRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted && "Cannot call beginDeferredRenderPass() while frame is not in progress");
		assert(commandBuffer == getCurrentCommandBuffer() && "Cannot begin renderpass from a different frame");

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass  = deferredRenderFrameBuffer.renderPass;
		renderPassInfo.framebuffer = deferredRenderFrameBuffer.frameBuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = {
			static_cast<uint32_t>(deferredRenderFrameBuffer.width),
			static_cast<uint32_t>(deferredRenderFrameBuffer.height)
		};

		// 5 clear values: position, normal, albedo, emission, depth
		std::array<VkClearValue, 5> clearValues{};
		clearValues[0].color        = { 0.0f, 0.0f, 0.0f, 0.0f }; // position + metallic
		clearValues[1].color        = { 0.0f, 0.0f, 0.0f, 0.0f }; // normal + roughness
		clearValues[2].color        = { 0.0f, 0.0f, 0.0f, 0.0f }; // albedo (w=0 = background)
		clearValues[3].color        = { 0.0f, 0.0f, 0.0f, 0.0f }; // emission
		clearValues[4].depthStencil = { 1.0f, 0 };
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues    = clearValues.data();

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.x        = 0.0f;
		viewport.y        = 0.0f;
		viewport.width    = static_cast<float>(deferredRenderFrameBuffer.width);
		viewport.height   = static_cast<float>(deferredRenderFrameBuffer.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{
			{0, 0},
			{ static_cast<uint32_t>(deferredRenderFrameBuffer.width),
			  static_cast<uint32_t>(deferredRenderFrameBuffer.height) }
		};
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void BGLRenderer::blitGBufferDepthToSwapchain(VkCommandBuffer commandBuffer)
	{
		VkImage srcDepth = deferredRenderFrameBuffer.depth.image;
		VkImage dstDepth = bglSwapChain->getDepthImage(currentImageIndex);
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

		VkImageMemoryBarrier srcBarrier{};
		srcBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		srcBarrier.oldLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		srcBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		srcBarrier.image               = srcDepth;
		srcBarrier.subresourceRange    = { aspect, 0, 1, 0, 1 };
		srcBarrier.srcAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		srcBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;

		// Use UNDEFINED as old layout so Vulkan discards previous swapchain depth contents (correct on first frame too)
		VkImageMemoryBarrier dstBarrier{};
		dstBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		dstBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
		dstBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		dstBarrier.image               = dstDepth;
		dstBarrier.subresourceRange    = { aspect, 0, 1, 0, 1 };
		dstBarrier.srcAccessMask       = 0;
		dstBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;

		std::array<VkImageMemoryBarrier, 2> preBarriers = { srcBarrier, dstBarrier };
		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr,
			static_cast<uint32_t>(preBarriers.size()), preBarriers.data());

		VkImageBlit region{};
		region.srcSubresource = { aspect, 0, 0, 1 };
		region.srcOffsets[0]  = { 0, 0, 0 };
		region.srcOffsets[1]  = { deferredRenderFrameBuffer.width, deferredRenderFrameBuffer.height, 1 };
		region.dstSubresource = { aspect, 0, 0, 1 };
		region.dstOffsets[0]  = { 0, 0, 0 };
		region.dstOffsets[1]  = { static_cast<int32_t>(bglSwapChain->width()), static_cast<int32_t>(bglSwapChain->height()), 1 };

		vkCmdBlitImage(commandBuffer,
			srcDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstDepth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region, VK_FILTER_NEAREST);

		srcBarrier.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		srcBarrier.newLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		srcBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		dstBarrier.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		dstBarrier.newLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dstBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		std::array<VkImageMemoryBarrier, 2> postBarriers = { srcBarrier, dstBarrier };
		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			0, 0, nullptr, 0, nullptr,
			static_cast<uint32_t>(postBarriers.size()), postBarriers.data());
	}

	void BGLRenderer::createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment* attachment, uint32_t width, uint32_t height)
	{
		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (format >= VK_FORMAT_D16_UNORM_S8_UINT)
				aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo image{};
		image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width  = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		// Depth attachments additionally need TRANSFER_SRC_BIT so the G-buffer depth can be blitted to the swapchain depth
		VkImageUsageFlags extraFlags = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT | extraFlags;

		VkMemoryAllocateInfo memAlloc{};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		VkMemoryRequirements memReqs;

		VK_CHECK(vkCreateImage(BGLDevice::device(), &image, nullptr, &attachment->image));
		vkGetImageMemoryRequirements(BGLDevice::device(), attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		//memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		memAlloc.memoryTypeIndex = bglDevice.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VK_CHECK(vkAllocateMemory(BGLDevice::device(), &memAlloc, nullptr, &attachment->mem));
		VK_CHECK(vkBindImageMemory(BGLDevice::device(), attachment->image, attachment->mem, 0));

		VkImageViewCreateInfo imageView{};
		imageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK(vkCreateImageView(BGLDevice::device(), &imageView, nullptr, &attachment->view));
	}

	void BGLRenderer::prepareDeferredRenderFrameBuffer()
	{
		// Match swapchain resolution — no reason to render G-buffer larger than the display
		auto ext = bglSwapChain->getSwapChainExtent();
		deferredRenderFrameBuffer.width  = ext.width;
		deferredRenderFrameBuffer.height = ext.height;

		// Color attachments

		const uint32_t dw = deferredRenderFrameBuffer.width;
		const uint32_t dh = deferredRenderFrameBuffer.height;

		// (World space) Positions — R32 for precision at large distances
		createAttachment(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.position, dw, dh);
		// (World space) Normals
		createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.normal,   dw, dh);
		// Albedo (color)
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM,      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.albedo,   dw, dh);
		// Emission (RGB, sRGB-encoded)
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM,      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.emission, dw, dh);
		// Depth attachment
		VkFormat depthFormat = bglSwapChain->findDepthFormat();
		createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &deferredRenderFrameBuffer.depth, dw, dh);

		// Set up separate renderpass with references to the color and depth attachments
		// Attachment order: 0=position, 1=normal, 2=albedo, 3=emission, 4=depth
		std::array<VkAttachmentDescription, 5> attachmentDescs = {};

		for (uint32_t i = 0; i < 5; ++i)
		{
			attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			if (i == 4)
			{
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			}
			else
			{
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}

		// Formats
		attachmentDescs[0].format = deferredRenderFrameBuffer.position.format;
		attachmentDescs[1].format = deferredRenderFrameBuffer.normal.format;
		attachmentDescs[2].format = deferredRenderFrameBuffer.albedo.format;
		attachmentDescs[3].format = deferredRenderFrameBuffer.emission.format;
		attachmentDescs[4].format = deferredRenderFrameBuffer.depth.format;

		std::vector<VkAttachmentReference> colorReferences;
		colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 4;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.pColorAttachments = colorReferences.data();
		subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpass.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for attachment layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.pAttachments = attachmentDescs.data();
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 2;
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &renderPassInfo, nullptr, &deferredRenderFrameBuffer.renderPass));

		std::array<VkImageView, 5> attachments;
		attachments[0] = deferredRenderFrameBuffer.position.view;
		attachments[1] = deferredRenderFrameBuffer.normal.view;
		attachments[2] = deferredRenderFrameBuffer.albedo.view;
		attachments[3] = deferredRenderFrameBuffer.emission.view;
		attachments[4] = deferredRenderFrameBuffer.depth.view;

		VkFramebufferCreateInfo fbufCreateInfo = {};
		fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbufCreateInfo.pNext = NULL;
		fbufCreateInfo.renderPass = deferredRenderFrameBuffer.renderPass;
		fbufCreateInfo.pAttachments = attachments.data();
		fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbufCreateInfo.width = deferredRenderFrameBuffer.width;
		fbufCreateInfo.height = deferredRenderFrameBuffer.height;
		fbufCreateInfo.layers = 1;
		VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbufCreateInfo, nullptr, &deferredRenderFrameBuffer.frameBuffer));

		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler{};
		sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler.magFilter = VK_FILTER_NEAREST;
		sampler.minFilter = VK_FILTER_NEAREST;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK(vkCreateSampler(BGLDevice::device(), &sampler, nullptr, &deferredRenderFrameBuffer.sampler));
	}
	
	void BGLRenderer::destroyDeferredFrameBuffer()
	{
		auto destroyAtt = [](FrameBufferAttachment& att) {
			vkDestroyImageView(BGLDevice::device(), att.view,  nullptr);
			vkDestroyImage    (BGLDevice::device(), att.image, nullptr);
			vkFreeMemory      (BGLDevice::device(), att.mem,   nullptr);
			att.view = VK_NULL_HANDLE; att.image = VK_NULL_HANDLE; att.mem = VK_NULL_HANDLE;
		};
		vkDestroyFramebuffer(BGLDevice::device(), deferredRenderFrameBuffer.frameBuffer, nullptr);
		vkDestroyRenderPass (BGLDevice::device(), deferredRenderFrameBuffer.renderPass,  nullptr);
		vkDestroySampler    (BGLDevice::device(), deferredRenderFrameBuffer.sampler,     nullptr);
		deferredRenderFrameBuffer.frameBuffer = VK_NULL_HANDLE;
		deferredRenderFrameBuffer.renderPass  = VK_NULL_HANDLE;
		deferredRenderFrameBuffer.sampler     = VK_NULL_HANDLE;
		destroyAtt(deferredRenderFrameBuffer.position);
		destroyAtt(deferredRenderFrameBuffer.normal);
		destroyAtt(deferredRenderFrameBuffer.albedo);
		destroyAtt(deferredRenderFrameBuffer.emission);
		destroyAtt(deferredRenderFrameBuffer.depth);
	}

	void BGLRenderer::destroyBloomMips()
	{
		for (auto& buf : bloomMips) {
			vkDestroySampler    (BGLDevice::device(), buf.sampler,        nullptr);
			vkDestroyRenderPass (BGLDevice::device(), buf.renderPassClear, nullptr);
			vkDestroyRenderPass (BGLDevice::device(), buf.renderPassLoad,  nullptr);
			vkDestroyFramebuffer(BGLDevice::device(), buf.frameBuffer,     nullptr);
			vkDestroyImageView  (BGLDevice::device(), buf.color.view,      nullptr);
			vkDestroyImage      (BGLDevice::device(), buf.color.image,     nullptr);
			vkFreeMemory        (BGLDevice::device(), buf.color.mem,       nullptr);
			buf.sampler        = VK_NULL_HANDLE;
			buf.renderPassClear = VK_NULL_HANDLE;
			buf.renderPassLoad  = VK_NULL_HANDLE;
			buf.frameBuffer    = VK_NULL_HANDLE;
			buf.color.view     = VK_NULL_HANDLE;
			buf.color.image    = VK_NULL_HANDLE;
			buf.color.mem      = VK_NULL_HANDLE;
		}
	}

	void BGLRenderer::prepareBloomMips()
	{
		// Mip 0 is swapchain/4, preserving the window's aspect ratio.
		// Clamped so no mip is smaller than 8px in either axis.
		auto sc = bglSwapChain->getSwapChainExtent();
		const uint32_t baseW = std::max(sc.width  / 4, 8u);
		const uint32_t baseH = std::max(sc.height / 4, 8u);
		for (uint32_t i = 0; i < BLOOM_MIPS; i++) {
			BloomBuffer& buf = bloomMips[i];
			buf.width  = std::max(baseW >> i, 1u);
			buf.height = std::max(baseH >> i, 1u);

			createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &buf.color, buf.width, buf.height);

			VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments    = &colorRef;

			// Shared subpass dependencies
			std::array<VkSubpassDependency, 2> deps{};
			deps[0] = { VK_SUBPASS_EXTERNAL, 0,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_DEPENDENCY_BY_REGION_BIT };
			deps[1] = { 0, VK_SUBPASS_EXTERNAL,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_DEPENDENCY_BY_REGION_BIT };

			VkRenderPassCreateInfo rpInfo{};
			rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpInfo.subpassCount    = 1; rpInfo.pSubpasses   = &subpass;
			rpInfo.attachmentCount = 1;
			rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
			rpInfo.pDependencies   = deps.data();

			// CLEAR render pass — for downsample (overwrites the mip)
			VkAttachmentDescription clearDesc{};
			clearDesc.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
			clearDesc.samples        = VK_SAMPLE_COUNT_1_BIT;
			clearDesc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
			clearDesc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			clearDesc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			clearDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			clearDesc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			clearDesc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			rpInfo.pAttachments = &clearDesc;
			VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &buf.renderPassClear));

			// LOAD render pass — for upsample accumulation (additively blends into existing content)
			VkAttachmentDescription loadDesc = clearDesc;
			loadDesc.loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
			loadDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			rpInfo.pAttachments = &loadDesc;
			VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &buf.renderPassLoad));

			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = buf.renderPassClear; // compatible with both render passes
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments    = &buf.color.view;
			fbInfo.width = buf.width; fbInfo.height = buf.height; fbInfo.layers = 1;
			VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &buf.frameBuffer));

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter    = samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.maxAnisotropy = 1.0f; samplerInfo.maxLod = 1.0f;
			samplerInfo.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &buf.sampler));
		}
	}

	void BGLRenderer::beginBloomDownsamplePass(VkCommandBuffer commandBuffer, int mip)
	{
		assert(isFrameStarted);
		BloomBuffer& buf = bloomMips[mip];
		VkRenderPassBeginInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass      = buf.renderPassClear;
		rpInfo.framebuffer     = buf.frameBuffer;
		rpInfo.renderArea      = { {0,0}, {buf.width, buf.height} };
		VkClearValue cv{}; cv.color = {0,0,0,0};
		rpInfo.clearValueCount = 1; rpInfo.pClearValues = &cv;
		vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp{ 0,0,(float)buf.width,(float)buf.height,0,1 };
		VkRect2D   sc{ {0,0},{buf.width,buf.height} };
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);
		vkCmdSetScissor(commandBuffer, 0, 1, &sc);
	}

	void BGLRenderer::beginBloomUpsamplePass(VkCommandBuffer commandBuffer, int mip)
	{
		assert(isFrameStarted);
		BloomBuffer& buf = bloomMips[mip];
		VkRenderPassBeginInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass      = buf.renderPassLoad;
		rpInfo.framebuffer     = buf.frameBuffer;
		rpInfo.renderArea      = { {0,0}, {buf.width, buf.height} };
		rpInfo.clearValueCount = 0; // LOAD_OP_LOAD — no clear value needed
		vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp{ 0,0,(float)buf.width,(float)buf.height,0,1 };
		VkRect2D   sc{ {0,0},{buf.width,buf.height} };
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);
		vkCmdSetScissor(commandBuffer, 0, 1, &sc);
	}

	void BGLRenderer::createCommandBuffers()
	{
		commandBuffers.resize(BGLSwapChain::MAX_FRAMES_IN_FLIGHT);
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		// Primary command buffers can be submitted to device graphics queue for execution but can not be called by other command buffers
		// Secondary command buffers can not be submitted to the queue but can be called by other command buffers

		allocInfo.commandPool = bglDevice.getCommandPool();
		allocInfo.commandBufferCount = static_cast<uint32_t> (commandBuffers.size());

		if (vkAllocateCommandBuffers(BGLDevice::device(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("Failed to Allocate Command Buffers");
		}
	}

	void BGLRenderer::freeCommandBuffers()
	{
		std::cout << "Clearing Command Buffer\n";
		vkFreeCommandBuffers(
			BGLDevice::device(),
			bglDevice.getCommandPool(),
			static_cast<uint32_t>(commandBuffers.size()),
			commandBuffers.data());
		commandBuffers.clear();
	}
}

