#include "engine/renderer/bagel_renderer.hpp"
namespace bagel
{

	void BGLRenderer::prepareBloomMips()
	{
		// Mip 0 is swapchain/4, preserving the window's aspect ratio.
		// Clamped so no mip is smaller than 8px in either axis.
		auto sc = bglSwapChain->getSwapChainExtent();
		const uint32_t baseW = std::max(sc.width / 4, 8u);
		const uint32_t baseH = std::max(sc.height / 4, 8u);

		// Bloom is a positive-HDR blur with no alpha and no need for 16-bit precision, so
		// R11G11B10F (32bpp) halves bandwidth/memory vs RGBA16F (64bpp). Fall back to RGBA16F
		// if the device can't use it as a linear-filterable, color-blendable attachment.
		VkFormat bloomFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
		{
			VkFormatProperties fp{};
			vkGetPhysicalDeviceFormatProperties(bglDevice.getPhysicalDevice(), bloomFormat, &fp);
			const VkFormatFeatureFlags need =
				VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
				VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
				VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
				VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			if ((fp.optimalTilingFeatures & need) != need)
				bloomFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		}

		for (uint32_t i = 0; i < BLOOM_MIPS; i++)
		{
			BloomBuffer &buf = bloomMips[i];
			buf.width = std::max(baseW >> i, 1u);
			buf.height = std::max(baseH >> i, 1u);

			createAttachment(bloomFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &buf.color, buf.width, buf.height);

			VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorRef;

			// Shared subpass dependencies
			std::array<VkSubpassDependency, 2> deps{};
			deps[0] = {VK_SUBPASS_EXTERNAL, 0,
					   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
					   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					   VK_DEPENDENCY_BY_REGION_BIT};
			deps[1] = {0, VK_SUBPASS_EXTERNAL,
					   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					   VK_ACCESS_SHADER_READ_BIT,
					   VK_DEPENDENCY_BY_REGION_BIT};

			VkRenderPassCreateInfo rpInfo{};
			rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpInfo.subpassCount = 1;
			rpInfo.pSubpasses = &subpass;
			rpInfo.attachmentCount = 1;
			rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
			rpInfo.pDependencies = deps.data();

			// Downsample render pass: the fullscreen triangle overwrites every texel, so the
			// previous contents are discarded (DONT_CARE) rather than cleared — no clear write.
			VkAttachmentDescription clearDesc{};
			clearDesc.format = bloomFormat;
			clearDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			clearDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			clearDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			clearDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			clearDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			clearDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			clearDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			rpInfo.pAttachments = &clearDesc;
			VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &buf.renderPassClear));

			// LOAD render pass — for upsample accumulation (additively blends into existing content)
			VkAttachmentDescription loadDesc = clearDesc;
			loadDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			loadDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			rpInfo.pAttachments = &loadDesc;
			VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &buf.renderPassLoad));

			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = buf.renderPassClear; // compatible with both render passes
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments = &buf.color.view;
			fbInfo.width = buf.width;
			fbInfo.height = buf.height;
			fbInfo.layers = 1;
			VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &buf.frameBuffer));

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.maxAnisotropy = 1.0f;
			samplerInfo.maxLod = 1.0f;
			samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &buf.sampler));
		}
	}

	void BGLRenderer::beginBloomDownsamplePass(VkCommandBuffer commandBuffer, uint8_t mip)
	{
		assert(isFrameStarted);
		BloomBuffer &buf = bloomMips[mip];
		VkRenderPassBeginInfo rpInfo{};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass = buf.renderPassClear;
		rpInfo.framebuffer = buf.frameBuffer;
		rpInfo.renderArea = {{0, 0}, {buf.width, buf.height}};
		VkClearValue cv{};
		cv.color = {0, 0, 0, 0};
		rpInfo.clearValueCount = 1;
		rpInfo.pClearValues = &cv;
		vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp{0, 0, (float)buf.width, (float)buf.height, 0, 1};
		VkRect2D sc{{0, 0}, {buf.width, buf.height}};
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);
		vkCmdSetScissor(commandBuffer, 0, 1, &sc);
	}

	void BGLRenderer::beginBloomUpsamplePass(VkCommandBuffer commandBuffer, uint8_t mip)
	{
		assert(isFrameStarted);
		BloomBuffer &buf = bloomMips[mip];
		VkRenderPassBeginInfo rpInfo{};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass = buf.renderPassLoad;
		rpInfo.framebuffer = buf.frameBuffer;
		rpInfo.renderArea = {{0, 0}, {buf.width, buf.height}};
		rpInfo.clearValueCount = 0; // LOAD_OP_LOAD — no clear value needed
		vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp{0, 0, (float)buf.width, (float)buf.height, 0, 1};
		VkRect2D sc{{0, 0}, {buf.width, buf.height}};
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);
		vkCmdSetScissor(commandBuffer, 0, 1, &sc);
	}
	void BGLRenderer::destroyBloomMips()
	{
		for (auto &buf : bloomMips)
		{
			vkDestroySampler(BGLDevice::device(), buf.sampler, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), buf.renderPassClear, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), buf.renderPassLoad, nullptr);
			vkDestroyFramebuffer(BGLDevice::device(), buf.frameBuffer, nullptr);
			vkDestroyImageView(BGLDevice::device(), buf.color.view, nullptr);
			vkDestroyImage(BGLDevice::device(), buf.color.image, nullptr);
			vkFreeMemory(BGLDevice::device(), buf.color.mem, nullptr);
			buf.sampler = VK_NULL_HANDLE;
			buf.renderPassClear = VK_NULL_HANDLE;
			buf.renderPassLoad = VK_NULL_HANDLE;
			buf.frameBuffer = VK_NULL_HANDLE;
			buf.color.view = VK_NULL_HANDLE;
			buf.color.image = VK_NULL_HANDLE;
			buf.color.mem = VK_NULL_HANDLE;
		}
	}
}