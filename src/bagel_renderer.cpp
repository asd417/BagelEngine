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
		prepareRadiosityBuffer();
		prepareSmaaEdgeBuffer();
		prepareShadowMapBuffer();
		prepareTransparentPass();          // depends on the radiosity buffer + G-buffer depth
		buildTransparentFramebuffers();
		gbufferExtent = bglSwapChain->getSwapChainExtent();
	}
	BGLRenderer::~BGLRenderer()
	{
		std::cout << "Destroying BGLRenderer\n";
		destroyTransparentFramebuffers();
		if (transparentRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(BGLDevice::device(), transparentRenderPass, nullptr);
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
			destroyRadiosityBuffer();
			destroySmaaEdgeBuffer();
			prepareDeferredRenderFrameBuffer();
			prepareBloomMips();
			prepareRadiosityBuffer();
			prepareSmaaEdgeBuffer();
			gbufferExtent = newExtent;
			gbufferRecreated = true;
		}
		// Swapchain image views are recreated on every swapchain rebuild (even without an extent
		// change), so the MRT framebuffers — which reference them and the radiosity view — must be
		// rebuilt every time. Guarded so the first call (before the render pass exists) is skipped.
		if (transparentRenderPass != VK_NULL_HANDLE) {
			destroyTransparentFramebuffers();
			buildTransparentFramebuffers();
		}
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

		// 4 clear values: normal, albedo, emission, depth
		std::array<VkClearValue, 4> clearValues{};
		clearValues[0].color        = { 0.0f, 0.0f, 0.0f, 0.0f }; // normal + roughness
		clearValues[1].color        = { 0.0f, 0.0f, 0.0f, 0.0f }; // albedo (w=0 = background)
		clearValues[2].color        = { 0.0f, 0.0f, 0.0f, 0.0f }; // emission
		clearValues[3].depthStencil = { 1.0f, 0 };                 // depth
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
		srcBarrier.oldLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		srcBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		srcBarrier.image               = srcDepth;
		srcBarrier.subresourceRange    = { aspect, 0, 1, 0, 1 };
		srcBarrier.srcAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		srcBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;

		// Use UNDEFINED as old layout so Vulkan discards previous swapchain depth contents (correct on
		// first frame too) — the blit overwrites the whole image, and assuming a specific prior layout
		// (e.g. READ_ONLY) trips a layout-mismatch validation error when the swapchain pass left it
		// in DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
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
		srcBarrier.newLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		dstBarrier.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		dstBarrier.newLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dstBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		std::array<VkImageMemoryBarrier, 2> postBarriers = { srcBarrier, dstBarrier };
		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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

		// (World space) Normals — oct-encoded xy + roughness + metallic, 16 bits each
		createAttachment(VK_FORMAT_R16G16B16A16_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.normal,   dw, dh);
		// Albedo (color)
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.albedo,   dw, dh);
		// Emission (RGB, sRGB-encoded)
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.emission, dw, dh);
		// Depth — also sampled in the lighting pass for position reconstruction
		VkFormat depthFormat = bglSwapChain->findDepthFormat();
		createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &deferredRenderFrameBuffer.depth, dw, dh);

		// Set up separate renderpass with references to the color and depth attachments
		// Attachment order: 0=normal, 1=albedo, 2=emission, 3=depth
		std::array<VkAttachmentDescription, 4> attachmentDescs = {};

		for (uint32_t i = 0; i < 4; ++i)
		{
			attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			if (i == 3)
			{
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			}
			else
			{
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}

		// Formats
		attachmentDescs[0].format = deferredRenderFrameBuffer.normal.format;
		attachmentDescs[1].format = deferredRenderFrameBuffer.albedo.format;
		attachmentDescs[2].format = deferredRenderFrameBuffer.emission.format;
		attachmentDescs[3].format = deferredRenderFrameBuffer.depth.format;

		std::vector<VkAttachmentReference> colorReferences;
		colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 3;
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

		std::array<VkImageView, 4> attachments;
		attachments[0] = deferredRenderFrameBuffer.normal.view;
		attachments[1] = deferredRenderFrameBuffer.albedo.view;
		attachments[2] = deferredRenderFrameBuffer.emission.view;
		attachments[3] = deferredRenderFrameBuffer.depth.view;

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
	
	void BGLRenderer::prepareShadowMapBuffer()
	{
		VkFormat depthFormat = bglSwapChain->findDepthFormat();
		VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT)
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		
		// One depth image per cascade, each at its own resolution
		VkImageCreateInfo image{};
		image.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image.imageType     = VK_IMAGE_TYPE_2D;
		image.format        = depthFormat;
		image.mipLevels     = 1;
		image.arrayLayers   = 1;
		image.samples       = VK_SAMPLE_COUNT_1_BIT;
		image.tiling        = VK_IMAGE_TILING_OPTIMAL;
		image.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format           = depthFormat;

		for (uint32_t i = 0; i < ShadowMapBuffer::CASCADE_COUNT; i++)
		{
			shadowMapBuffer.depth[i].format = depthFormat;

			image.extent        = { ShadowMapBuffer::RESOLUTIONS[i], ShadowMapBuffer::RESOLUTIONS[i], 1 };
			VK_CHECK(vkCreateImage(BGLDevice::device(), &image, nullptr, &shadowMapBuffer.depth[i].image));

			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements(BGLDevice::device(), shadowMapBuffer.depth[i].image, &memReqs);

			VkMemoryAllocateInfo memAlloc{};
			memAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memAlloc.allocationSize  = memReqs.size;
			memAlloc.memoryTypeIndex = bglDevice.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK(vkAllocateMemory(BGLDevice::device(), &memAlloc, nullptr, &shadowMapBuffer.depth[i].mem));
			VK_CHECK(vkBindImageMemory(BGLDevice::device(), shadowMapBuffer.depth[i].image, shadowMapBuffer.depth[i].mem, 0));

			viewInfo.image            = shadowMapBuffer.depth[i].image;
			viewInfo.subresourceRange = { aspectMask, 0, 1, 0, 1 };
			VK_CHECK(vkCreateImageView(BGLDevice::device(), &viewInfo, nullptr, &shadowMapBuffer.depth[i].view));
		}

		VkAttachmentDescription depthDesc{};
		depthDesc.format         = depthFormat;
		depthDesc.samples        = VK_SAMPLE_COUNT_1_BIT;
		depthDesc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthDesc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		depthDesc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthDesc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		depthDesc.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		VkAttachmentReference depthRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount    = 0;
		subpass.pDepthStencilAttachment = &depthRef;

		// The lighting pass samples the shadow map at arbitrary UVs, so these
		// dependencies must be framebuffer-global (no BY_REGION).
		std::array<VkSubpassDependency, 2> deps{};
		deps[0] = { VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			0 };
		deps[1] = { 0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			0 };

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments    = &depthDesc;
		rpInfo.subpassCount    = 1;
		rpInfo.pSubpasses      = &subpass;
		rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
		rpInfo.pDependencies   = deps.data();
		VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &shadowMapBuffer.renderPass));

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = shadowMapBuffer.renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.layers          = 1;
		for (uint32_t i = 0; i < ShadowMapBuffer::CASCADE_COUNT; i++) {
			fbInfo.width           = ShadowMapBuffer::RESOLUTIONS[i];
			fbInfo.height          = ShadowMapBuffer::RESOLUTIONS[i];
			fbInfo.pAttachments = &shadowMapBuffer.depth[i].view;
			VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &shadowMapBuffer.frameBuffers[i]));
		}

		// Compare sampler for sampler2DShadow in the lighting pass
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter        = VK_FILTER_LINEAR;
		samplerInfo.minFilter        = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerInfo.compareEnable    = VK_TRUE;
		samplerInfo.compareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
		samplerInfo.maxAnisotropy    = 1.0f;
		samplerInfo.maxLod           = 1.0f;
		VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &shadowMapBuffer.sampler));
	}

	void BGLRenderer::beginShadowMapPass(VkCommandBuffer commandBuffer, uint32_t cascade)
	{
		assert(isFrameStarted);
		assert(cascade < ShadowMapBuffer::CASCADE_COUNT);
		VkRenderPassBeginInfo rpInfo{};
		rpInfo.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass  = shadowMapBuffer.renderPass;
		rpInfo.framebuffer = shadowMapBuffer.frameBuffers[cascade];
		uint32_t res = ShadowMapBuffer::RESOLUTIONS[cascade];
		rpInfo.renderArea  = { {0, 0}, {res, res} };
		VkClearValue cv{}; cv.depthStencil = { 1.0f, 0 };
		rpInfo.clearValueCount = 1;
		rpInfo.pClearValues    = &cv;
		vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp{ 0, 0, (float)res, (float)res, 0.0f, 1.0f };
		VkRect2D   sc{ {0, 0}, {res, res} };
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);
		vkCmdSetScissor(commandBuffer, 0, 1, &sc);
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

	void BGLRenderer::prepareRadiosityBuffer()
	{
		auto sc = bglSwapChain->getSwapChainExtent();
		radiosityBuffer.width  = sc.width;
		radiosityBuffer.height = sc.height;

		createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&radiosityBuffer.color,
			radiosityBuffer.width, radiosityBuffer.height);

		VkAttachmentDescription desc{};
		desc.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
		desc.samples        = VK_SAMPLE_COUNT_1_BIT;
		desc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
		desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		desc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments    = &colorRef;

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
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments    = &desc;
		rpInfo.subpassCount    = 1;
		rpInfo.pSubpasses      = &subpass;
		rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
		rpInfo.pDependencies   = deps.data();
		VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &radiosityBuffer.renderPass));

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = radiosityBuffer.renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments    = &radiosityBuffer.color.view;
		fbInfo.width           = radiosityBuffer.width;
		fbInfo.height          = radiosityBuffer.height;
		fbInfo.layers          = 1;
		VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &radiosityBuffer.frameBuffer));

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter    = samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.maxLod       = 1.0f;
		samplerInfo.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &radiosityBuffer.sampler));
	}

	void BGLRenderer::destroyRadiosityBuffer()
	{
		vkDestroySampler    (BGLDevice::device(), radiosityBuffer.sampler,      nullptr);
		vkDestroyRenderPass (BGLDevice::device(), radiosityBuffer.renderPass,   nullptr);
		vkDestroyFramebuffer(BGLDevice::device(), radiosityBuffer.frameBuffer,  nullptr);
		vkDestroyImageView  (BGLDevice::device(), radiosityBuffer.color.view,   nullptr);
		vkDestroyImage      (BGLDevice::device(), radiosityBuffer.color.image,  nullptr);
		vkFreeMemory        (BGLDevice::device(), radiosityBuffer.color.mem,    nullptr);
		radiosityBuffer.sampler      = VK_NULL_HANDLE;
		radiosityBuffer.renderPass   = VK_NULL_HANDLE;
		radiosityBuffer.frameBuffer  = VK_NULL_HANDLE;
		radiosityBuffer.color.view   = VK_NULL_HANDLE;
		radiosityBuffer.color.image  = VK_NULL_HANDLE;
		radiosityBuffer.color.mem    = VK_NULL_HANDLE;
	}

	void BGLRenderer::beginRadiosityPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted);
		VkRenderPassBeginInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass      = radiosityBuffer.renderPass;
		rpInfo.framebuffer     = radiosityBuffer.frameBuffer;
		rpInfo.renderArea      = { {0,0}, {radiosityBuffer.width, radiosityBuffer.height} };
		VkClearValue cv{}; cv.color = {0,0,0,0};
		rpInfo.clearValueCount = 1; rpInfo.pClearValues = &cv;
		vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp{ 0,0,(float)radiosityBuffer.width,(float)radiosityBuffer.height,0,1 };
		VkRect2D   sc{ {0,0},{radiosityBuffer.width,radiosityBuffer.height} };
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);
		vkCmdSetScissor(commandBuffer, 0, 1, &sc);
	}

	void BGLRenderer::prepareSmaaEdgeBuffer()
	{
		// Single RG8 color target, screen-sized. Same render-pass shape as the radiosity buffer:
		// CLEAR on load (the edge shader discards non-edge pixels, so cleared 0 = "no edge"),
		// finalLayout SHADER_READ so later passes / the debug view can sample it.
		auto sc = bglSwapChain->getSwapChainExtent();
		smaaEdgeBuffer.width  = sc.width;
		smaaEdgeBuffer.height = sc.height;

		createAttachment(VK_FORMAT_R8G8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&smaaEdgeBuffer.color,
			smaaEdgeBuffer.width, smaaEdgeBuffer.height);

		VkAttachmentDescription desc{};
		desc.format         = VK_FORMAT_R8G8_UNORM;
		desc.samples        = VK_SAMPLE_COUNT_1_BIT;
		desc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
		desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		desc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments    = &colorRef;

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
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments    = &desc;
		rpInfo.subpassCount    = 1;
		rpInfo.pSubpasses      = &subpass;
		rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
		rpInfo.pDependencies   = deps.data();
		VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &smaaEdgeBuffer.renderPass));

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = smaaEdgeBuffer.renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments    = &smaaEdgeBuffer.color.view;
		fbInfo.width           = smaaEdgeBuffer.width;
		fbInfo.height          = smaaEdgeBuffer.height;
		fbInfo.layers          = 1;
		VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &smaaEdgeBuffer.frameBuffer));

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter    = samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.maxLod       = 1.0f;
		samplerInfo.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &smaaEdgeBuffer.sampler));
	}

	void BGLRenderer::destroySmaaEdgeBuffer()
	{
		vkDestroySampler    (BGLDevice::device(), smaaEdgeBuffer.sampler,     nullptr);
		vkDestroyRenderPass (BGLDevice::device(), smaaEdgeBuffer.renderPass,  nullptr);
		vkDestroyFramebuffer(BGLDevice::device(), smaaEdgeBuffer.frameBuffer, nullptr);
		vkDestroyImageView  (BGLDevice::device(), smaaEdgeBuffer.color.view,  nullptr);
		vkDestroyImage      (BGLDevice::device(), smaaEdgeBuffer.color.image, nullptr);
		vkFreeMemory        (BGLDevice::device(), smaaEdgeBuffer.color.mem,   nullptr);
		smaaEdgeBuffer.sampler     = VK_NULL_HANDLE;
		smaaEdgeBuffer.renderPass  = VK_NULL_HANDLE;
		smaaEdgeBuffer.frameBuffer = VK_NULL_HANDLE;
		smaaEdgeBuffer.color.view  = VK_NULL_HANDLE;
		smaaEdgeBuffer.color.image = VK_NULL_HANDLE;
		smaaEdgeBuffer.color.mem   = VK_NULL_HANDLE;
	}

	void BGLRenderer::beginSmaaEdgePass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted);
		VkRenderPassBeginInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass      = smaaEdgeBuffer.renderPass;
		rpInfo.framebuffer     = smaaEdgeBuffer.frameBuffer;
		rpInfo.renderArea      = { {0,0}, {smaaEdgeBuffer.width, smaaEdgeBuffer.height} };
		VkClearValue cv{}; cv.color = {0,0,0,0};
		rpInfo.clearValueCount = 1; rpInfo.pClearValues = &cv;
		vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp{ 0,0,(float)smaaEdgeBuffer.width,(float)smaaEdgeBuffer.height,0,1 };
		VkRect2D   sc{ {0,0},{smaaEdgeBuffer.width,smaaEdgeBuffer.height} };
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);
		vkCmdSetScissor(commandBuffer, 0, 1, &sc);
	}

	void BGLRenderer::prepareTransparentPass()
	{
		// Radiosity-only forward transparent pass: blends HDR transparent lighting into the
		// radiosity buffer (which bloom and composite read), depth-testing read-only against the
		// opaque G-buffer depth. No swapchain/tonemap here — composite handles display.
		std::array<VkAttachmentDescription, 2> att{};
		// 0: radiosity HDR — LOAD opaque lighting, blend transparent over it, keep readable for bloom
		att[0].format         = VK_FORMAT_R16G16B16A16_SFLOAT;
		att[0].samples        = VK_SAMPLE_COUNT_1_BIT;
		att[0].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
		att[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		att[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		att[0].initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		att[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// 1: opaque G-buffer depth — LOAD, read-only depth test (no write). STORE is required:
		// DONT_CARE lets the driver discard the depth contents after this pass, which breaks every
		// later reader of the G-buffer depth (composite world-pos reconstruction, SMAA depth mode,
		// the depth blit). The pass doesn't write depth, but the contents must survive it.
		att[1].format         = deferredRenderFrameBuffer.depth.format;
		att[1].samples        = VK_SAMPLE_COUNT_1_BIT;
		att[1].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
		att[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		att[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		att[1].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		att[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount    = 1;
		subpass.pColorAttachments       = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		std::array<VkSubpassDependency, 2> deps{};
		// Wait for the radiosity pass color write and its G-buffer depth sample (plus the depth write) before blending.
		deps[0] = { VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT };
		// Make the radiosity blend visible to the bloom downsample / composite shader reads.
		deps[1] = { 0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT };

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = static_cast<uint32_t>(att.size());
		rpInfo.pAttachments    = att.data();
		rpInfo.subpassCount    = 1;
		rpInfo.pSubpasses      = &subpass;
		rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
		rpInfo.pDependencies   = deps.data();
		VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &transparentRenderPass));
	}

	void BGLRenderer::buildTransparentFramebuffers()
	{
		// Single full-screen framebuffer: the radiosity color + the opaque G-buffer depth.
		auto ext = bglSwapChain->getSwapChainExtent();
		std::array<VkImageView, 2> views{
			radiosityBuffer.color.view,
			deferredRenderFrameBuffer.depth.view
		};
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = transparentRenderPass;
		fbInfo.attachmentCount = static_cast<uint32_t>(views.size());
		fbInfo.pAttachments    = views.data();
		fbInfo.width           = ext.width;
		fbInfo.height          = ext.height;
		fbInfo.layers          = 1;
		VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &transparentFramebuffer));
	}

	void BGLRenderer::destroyTransparentFramebuffers()
	{
		if (transparentFramebuffer != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(BGLDevice::device(), transparentFramebuffer, nullptr);
			transparentFramebuffer = VK_NULL_HANDLE;
		}
	}

	void BGLRenderer::beginTransparentPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted);
		auto ext = bglSwapChain->getSwapChainExtent();
		VkRenderPassBeginInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass      = transparentRenderPass;
		rpInfo.framebuffer     = transparentFramebuffer;
		rpInfo.renderArea      = { {0,0}, ext };
		rpInfo.clearValueCount = 0; // all attachments LOAD
		vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp{ 0,0,(float)ext.width,(float)ext.height,0,1 };
		VkRect2D   sc{ {0,0}, ext };
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);
		vkCmdSetScissor(commandBuffer, 0, 1, &sc);
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

