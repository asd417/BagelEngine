#include "bagel_renderer.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

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

		//Deferred Rendering
		prepareDeferredRenderFrameBuffer();
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

		if (result != VK_SUCCESS && VK_SUBOPTIMAL_KHR) {
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
		//Pipeline relies on swapchain so the createPipeline() must follow after creating a new swapchain
		// if render pass is compatible do nothing else
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

	//Deferred rendering
	

	void BGLRenderer::createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment* attachment)
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
		image.extent.width = deferredRenderFrameBuffer.width;
		image.extent.height = deferredRenderFrameBuffer.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;

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
		// Note: Instead of using fixed sizes, one could also match the window size and recreate the attachments on resize
		deferredRenderFrameBuffer.width = 2048;
		deferredRenderFrameBuffer.height = 2048;

		// Color attachments

		// (World space) Positions
		createAttachment(
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&deferredRenderFrameBuffer.position);

		// (World space) Normals
		createAttachment(
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&deferredRenderFrameBuffer.normal);

		// Albedo (color)
		createAttachment(
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&deferredRenderFrameBuffer.albedo);

		// Depth attachment
		VkFormat depthFormat = bglSwapChain->findDepthFormat();

		createAttachment(
			depthFormat,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			&deferredRenderFrameBuffer.depth);

		// Set up separate renderpass with references to the color and depth attachments
		std::array<VkAttachmentDescription, 4> attachmentDescs = {};

		// Init attachment properties
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
		attachments[0] = deferredRenderFrameBuffer.position.view;
		attachments[1] = deferredRenderFrameBuffer.normal.view;
		attachments[2] = deferredRenderFrameBuffer.albedo.view;
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

