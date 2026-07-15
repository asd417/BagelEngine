#include "engine/renderer/bagel_renderer.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include <stdexcept>
#include <array>
#include <iostream>

namespace bagel
{
	BGLRenderer::BGLRenderer(BGLWindow &w, BGLDevice &d) : bglWindow{w}, bglDevice{d}
	{
		recreateSwapChain();
		createCommandBuffers();
		prepareDeferredRenderFrameBuffer();
		prepareBloomMips();
		prepareRadiosityBuffer();
		prepareCompositeBuffer();
		prepareSmaaEdgeBuffer();
		prepareSmaaWeightBuffer();
		prepareShadowMapBuffer();
		prepareTransparentPass(); // depends on the radiosity buffer + G-buffer depth
		buildTransparentFramebuffers();
		gbufferExtent = bglSwapChain->getSwapChainExtent();
	}
	BGLRenderer::~BGLRenderer()
	{
		std::cout << "Destroying BGLRenderer\n";
		destroyTransparentFramebuffers();
		if (transparentRenderPass != VK_NULL_HANDLE)
			vkDestroyRenderPass(BGLDevice::device(), transparentRenderPass, nullptr);
		freeCommandBuffers();
		std::cout << "Finished Destroying BGLRenderer\n";
	}

	VkCommandBuffer BGLRenderer::beginPrimaryCMD()
	{
		assert(!isFrameStarted && "Can not call beginPrimaryCMD() while the frame is already started");
		auto result = bglSwapChain->acquireNextImage(&currentImageIndex);

		// detect if the surface is no longer compatible with the swapchain
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			recreateSwapChain();
			return nullptr;
		}

		if (result == VK_SUBOPTIMAL_KHR)
		{
			recreateSwapChain();
			return nullptr;
		}

		if (result != VK_SUCCESS)
		{
			throw std::runtime_error("failed to acquire swapchain image");
		}

		isFrameStarted = true;
		auto commandBuffer = getCurrentCommandBuffer();

		// Record draw commands to each command buffers
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to begin recording draw command to command buffers");
		}
		return commandBuffer;
	}

	void BGLRenderer::endPrimaryCMD()
	{
		assert(isFrameStarted && "Cannot call endFrame() while frame is not in progress");

		auto commandBuffer = getCurrentCommandBuffer();
		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to record command buffer");
		}

		auto result = bglSwapChain->submitCommandBuffers(&commandBuffer, &currentImageIndex);
		// VK_SUBOPTIMAL_KHR means the swapchain no longer matches the surface but can be used to present on the surface
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || bglWindow.wasWindowResized())
		{
			bglWindow.resetWindowResizedFlag();
			recreateSwapChain();
		}
		else if (result != VK_SUCCESS)
		{
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
		renderPassInfo.framebuffer = bglSwapChain->getFrameBuffer(static_cast<uint8_t>(currentImageIndex));

		renderPassInfo.renderArea.offset = {0, 0};
		// Make sure to use the swapchain extent not the window extent
		// because the swapchain extent may be larger then window extent which is the case in Mac retina display
		renderPassInfo.renderArea.extent = bglSwapChain->getSwapChainExtent();

		// Set the color that the frame buffer 'attachments' will clear to
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}}; // color attachment
		clearValues[1].depthStencil = {1.0f, 0};			// Depths stencil clear value

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
		VkRect2D scissor{{0, 0}, bglSwapChain->getSwapChainExtent()};
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	// vkCmdEndRenderPass
	void BGLRenderer::endCurrentRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted && "Cannot call endSwapChainRenderPass() while frame is not in progress");
		assert(commandBuffer == getCurrentCommandBuffer() && "Cannot end renderpass from a different frame");
		vkCmdEndRenderPass(commandBuffer);
	}

	void BGLRenderer::recreateSwapChain()
	{
		auto extent = bglWindow.getExtent();
		// If there is at least one dimension with length of 0(or sizeless), the program will pause which is during minimization.
		while (extent.width == 0 || extent.height == 0)
		{
			extent = bglWindow.getExtent();
			glfwWaitEvents();
		}
		// Wait for current swapchain to no longer be used
		vkDeviceWaitIdle(BGLDevice::device());
		if (bglSwapChain == nullptr)
		{
			bglSwapChain = std::make_unique<BGLSwapChain>(bglDevice, extent);
		}
		else
		{
			std::shared_ptr<BGLSwapChain> oldSwapChain = std::move(bglSwapChain);
			bglSwapChain = std::make_unique<BGLSwapChain>(bglDevice, extent, oldSwapChain);

			if (!oldSwapChain->compareSwapFormat(*bglSwapChain.get()))
			{
				throw std::runtime_error("Swap chain image depth format has changed!");
			}
		}
		// Rebuild G-buffer and bloom mips if the swapchain extent changed
		auto newExtent = bglSwapChain->getSwapChainExtent();
		if (gbufferExtent.width != 0 &&
			(newExtent.width != gbufferExtent.width || newExtent.height != gbufferExtent.height))
		{
			destroyDeferredFrameBuffer();
			destroyBloomMips();
			destroyRadiosityBuffer();
			destroyCompositeBuffer();
			destroySmaaEdgeBuffer();
			destroySmaaWeightBuffer();
			prepareDeferredRenderFrameBuffer();
			prepareBloomMips();
			prepareRadiosityBuffer();
			prepareCompositeBuffer();
			prepareSmaaEdgeBuffer();
			prepareSmaaWeightBuffer();
			gbufferExtent = newExtent;
			gbufferRecreated = true;
		}
		// Swapchain image views are recreated on every swapchain rebuild (even without an extent
		// change), so the MRT framebuffers — which reference them and the radiosity view — must be
		// rebuilt every time. Guarded so the first call (before the render pass exists) is skipped.
		if (transparentRenderPass != VK_NULL_HANDLE)
		{
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
		renderPassInfo.renderPass = deferredRenderFrameBuffer.renderPass;
		renderPassInfo.framebuffer = deferredRenderFrameBuffer.frameBuffer;
		renderPassInfo.renderArea.offset = {0, 0};
		renderPassInfo.renderArea.extent = {
			static_cast<uint32_t>(deferredRenderFrameBuffer.width),
			static_cast<uint32_t>(deferredRenderFrameBuffer.height)};

		// 4 clear values: normal, albedo, emission, depth
		std::array<VkClearValue, 4> clearValues{};
		clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // normal + roughness
		clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // albedo (w=0 = background)
		clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // emission
		clearValues[3].depthStencil = {1.0f, 0};		 // depth
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(deferredRenderFrameBuffer.width);
		viewport.height = static_cast<float>(deferredRenderFrameBuffer.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{
			{0, 0},
			{static_cast<uint32_t>(deferredRenderFrameBuffer.width),
			 static_cast<uint32_t>(deferredRenderFrameBuffer.height)}};
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void BGLRenderer::blitGBufferDepthToSwapchain(VkCommandBuffer commandBuffer)
	{
		VkImage srcDepth = deferredRenderFrameBuffer.depth.image;
		VkImage dstDepth = bglSwapChain->getDepthImage(static_cast<uint8_t>(currentImageIndex));
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

		VkImageMemoryBarrier srcBarrier{};
		srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		srcBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		srcBarrier.image = srcDepth;
		srcBarrier.subresourceRange = {aspect, 0, 1, 0, 1};
		srcBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		// Use UNDEFINED as old layout so Vulkan discards previous swapchain depth contents (correct on
		// first frame too) — the blit overwrites the whole image, and assuming a specific prior layout
		// (e.g. READ_ONLY) trips a layout-mismatch validation error when the swapchain pass left it
		// in DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
		VkImageMemoryBarrier dstBarrier{};
		dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		dstBarrier.image = dstDepth;
		dstBarrier.subresourceRange = {aspect, 0, 1, 0, 1};
		dstBarrier.srcAccessMask = 0;
		dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		std::array<VkImageMemoryBarrier, 2> preBarriers = {srcBarrier, dstBarrier};
		vkCmdPipelineBarrier(commandBuffer,
							 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 0, 0, nullptr, 0, nullptr,
							 static_cast<uint32_t>(preBarriers.size()), preBarriers.data());

		VkImageBlit region{};
		region.srcSubresource = {aspect, 0, 0, 1};
		region.srcOffsets[0] = {0, 0, 0};
		region.srcOffsets[1] = {deferredRenderFrameBuffer.width, deferredRenderFrameBuffer.height, 1};
		region.dstSubresource = {aspect, 0, 0, 1};
		region.dstOffsets[0] = {0, 0, 0};
		region.dstOffsets[1] = {static_cast<int32_t>(bglSwapChain->width()), static_cast<int32_t>(bglSwapChain->height()), 1};

		vkCmdBlitImage(commandBuffer,
					   srcDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   dstDepth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					   1, &region, VK_FILTER_NEAREST);

		srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		srcBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		dstBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dstBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		std::array<VkImageMemoryBarrier, 2> postBarriers = {srcBarrier, dstBarrier};
		vkCmdPipelineBarrier(commandBuffer,
							 VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
							 0, 0, nullptr, 0, nullptr,
							 static_cast<uint32_t>(postBarriers.size()), postBarriers.data());
	}

	void BGLRenderer::createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment *attachment, uint32_t width, uint32_t height)
	{
		VkImageAspectFlags aspectMask = 0;
		//VkImageLayout imageLayout;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			//imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (format >= VK_FORMAT_D16_UNORM_S8_UINT)
				aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			//imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo image{};
		image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		// Depth attachments additionally need TRANSFER_SRC_BIT so the G-buffer depth can be blitted to the swapchain depth
		VkImageUsageFlags extraFlags = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
										   ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT
										   : 0;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT | extraFlags;

		VkMemoryAllocateInfo memAlloc{};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		VkMemoryRequirements memReqs;

		VK_CHECK(vkCreateImage(BGLDevice::device(), &image, nullptr, &attachment->image));
		vkGetImageMemoryRequirements(BGLDevice::device(), attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		// memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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
		deferredRenderFrameBuffer.width = static_cast<uint16_t>(ext.width);
		deferredRenderFrameBuffer.height = static_cast<uint16_t>(ext.height);

		// Color attachments

		const uint32_t dw = deferredRenderFrameBuffer.width;
		const uint32_t dh = deferredRenderFrameBuffer.height;

		// (World space) Normals — oct-encoded xy + roughness + metallic, 16 bits each
		createAttachment(VK_FORMAT_R16G16B16A16_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.normal, dw, dh);
		// Albedo (color)
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &deferredRenderFrameBuffer.albedo, dw, dh);
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
		colorReferences.push_back({0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
		colorReferences.push_back({1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
		colorReferences.push_back({2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

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

	void BGLRenderer::destroyDeferredFrameBuffer()
	{
		auto destroyAtt = [](FrameBufferAttachment &att)
		{
			vkDestroyImageView(BGLDevice::device(), att.view, nullptr);
			vkDestroyImage(BGLDevice::device(), att.image, nullptr);
			vkFreeMemory(BGLDevice::device(), att.mem, nullptr);
			att.view = VK_NULL_HANDLE;
			att.image = VK_NULL_HANDLE;
			att.mem = VK_NULL_HANDLE;
		};
		vkDestroyFramebuffer(BGLDevice::device(), deferredRenderFrameBuffer.frameBuffer, nullptr);
		vkDestroyRenderPass(BGLDevice::device(), deferredRenderFrameBuffer.renderPass, nullptr);
		vkDestroySampler(BGLDevice::device(), deferredRenderFrameBuffer.sampler, nullptr);
		deferredRenderFrameBuffer.frameBuffer = VK_NULL_HANDLE;
		deferredRenderFrameBuffer.renderPass = VK_NULL_HANDLE;
		deferredRenderFrameBuffer.sampler = VK_NULL_HANDLE;
		destroyAtt(deferredRenderFrameBuffer.normal);
		destroyAtt(deferredRenderFrameBuffer.albedo);
		destroyAtt(deferredRenderFrameBuffer.emission);
		destroyAtt(deferredRenderFrameBuffer.depth);
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
		allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

		if (vkAllocateCommandBuffers(BGLDevice::device(), &allocInfo, commandBuffers.data()) != VK_SUCCESS)
		{
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
