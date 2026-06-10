#pragma once

#include <glm/gtc/constants.hpp>

#include "bagel_window.hpp"
#include "bagel_engine_device.hpp"
#include "bagel_engine_swap_chain.hpp"
#include "bagel_model.hpp"

#include <array>
#include <cassert>
#include <memory>
#include <vector>

namespace bagel
{
	struct FrameBufferAttachment
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format;
		const FrameBufferAttachment operator=(const FrameBufferAttachment &) = delete;
		~FrameBufferAttachment()
		{
			if (view == VK_NULL_HANDLE)
				return; // already explicitly destroyed
			vkDestroyImageView(BGLDevice::device(), view, nullptr);
			vkDestroyImage(BGLDevice::device(), image, nullptr);
			vkFreeMemory(BGLDevice::device(), mem, nullptr);
		}
	};

	struct FrameBuffer
	{
		int32_t width = 0, height = 0;
		VkFramebuffer frameBuffer = VK_NULL_HANDLE;
		FrameBufferAttachment normal, albedo, emission;
		FrameBufferAttachment depth;
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		const FrameBuffer operator=(const FrameBuffer &) = delete;
		~FrameBuffer()
		{
			if (frameBuffer == VK_NULL_HANDLE)
				return; // already explicitly destroyed
			vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), renderPass, nullptr);
			vkDestroySampler(BGLDevice::device(), sampler, nullptr);
		}
	};

	struct RadiosityBuffer
	{
		uint32_t width = 0;
		uint32_t height = 0;
		FrameBufferAttachment color{};
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkFramebuffer frameBuffer = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		~RadiosityBuffer()
		{
			vkDestroySampler(BGLDevice::device(), sampler, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), renderPass, nullptr);
			vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
		}
	};

	struct BloomBuffer
	{
		uint32_t width = 0;
		uint32_t height = 0;
		FrameBufferAttachment color{};
		VkRenderPass renderPassClear = VK_NULL_HANDLE; // LOAD_OP_CLEAR  — for downsample passes
		VkRenderPass renderPassLoad = VK_NULL_HANDLE;  // LOAD_OP_LOAD   — for upsample accumulation
		VkFramebuffer frameBuffer = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		~BloomBuffer()
		{
			vkDestroySampler(BGLDevice::device(), sampler, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), renderPassClear, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), renderPassLoad, nullptr);
			vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
		}
	};

	struct OffscreenPass
	{
		uint32_t width, height;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment color{};
		FrameBufferAttachment depth{};
		VkRenderPass renderPass;
		VkSampler sampler;
		uint32_t renderTargetHandle;
		VkDescriptorImageInfo colorImageInfo;
		~OffscreenPass()
		{
			std::cout << "Destroying OffscreenPass\n";
			vkDestroySampler(BGLDevice::device(), sampler, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), renderPass, nullptr);
			vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
			std::cout << "Finished Destroying OffscreenPass\n";
		}
	};

	struct ShadowMapBuffer
	{
		static constexpr uint32_t RESOLUTIONS[] = {2048, 2048, 1024, 1024}; // must match SHADOW_CASCADE_COUNT in bagel_frame_info.hpp
		static constexpr uint32_t CASCADE_COUNT = 4;	 // must match SHADOW_CASCADE_COUNT in bagel_frame_info.hpp

		FrameBufferAttachment depth[CASCADE_COUNT] = {}; // one depth image per cascade, RESOLUTIONS[i] square
		VkFramebuffer frameBuffers[CASCADE_COUNT] = {};
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		~ShadowMapBuffer()
		{
			if (renderPass == VK_NULL_HANDLE)
				return;
			vkDestroySampler(BGLDevice::device(), sampler, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), renderPass, nullptr);
			for (uint32_t i = 0; i < CASCADE_COUNT; i++)
			{
				vkDestroyFramebuffer(BGLDevice::device(), frameBuffers[i], nullptr);
				vkDestroyImageView(BGLDevice::device(), depth[i].view, nullptr);
				vkDestroyImage(BGLDevice::device(), depth[i].image, nullptr);
				vkFreeMemory(BGLDevice::device(), depth[i].mem, nullptr);
			}
		}
	};

	class BGLRenderer
	{
	public:
		static constexpr uint32_t BLOOM_MIPS = 3;

		BGLRenderer(BGLWindow &w, BGLDevice &d);
		~BGLRenderer();

		BGLRenderer(const BGLRenderer &) = delete;
		BGLRenderer &operator=(const BGLRenderer &) = delete;

		VkCommandBuffer beginPrimaryCMD();
		void endPrimaryCMD();

		void beginSwapChainRenderPass(VkCommandBuffer commandBuffer);
		void endCurrentRenderPass(VkCommandBuffer commandBuffer);

		// Forward transparent pass: blends HDR transparent lighting into the radiosity buffer,
		// depth-testing read-only against the opaque G-buffer depth. Runs after radiosity, before bloom.
		void beginTransparentPass(VkCommandBuffer commandBuffer);
		VkRenderPass getTransparentRenderPass() const { return transparentRenderPass; }

		bool isFrameInProgress() const { return isFrameStarted; }
		VkCommandBuffer getCurrentCommandBuffer() const
		{
			assert(isFrameStarted && "Cannot get command buffer when frame not in progress");
			return commandBuffers[currentFrameIndex];
		}

		size_t getSwapChainImageCount() const { return bglSwapChain->imageCount(); }
		VkRenderPass getSwapChainRenderPass() const { return bglSwapChain->getRenderPass(); }
		float getAspectRatio() const { return bglSwapChain->extentAspectRatio(); }
		VkExtent2D getExtent() const { return bglSwapChain->getSwapChainExtent(); }

		int getFrameIndex() const
		{
			assert(isFrameStarted && "Cannot get frame index when frame not in progress");
			return currentFrameIndex;
		}

		// Offscreen Render tasks
		void setUpOffScreenRenderPass(uint32_t textureWidth, uint32_t textureHeight);
		void createOffScreenRenderPass(uint32_t textureWidth, uint32_t textureHeight);
		void beginOffScreenRenderPass(VkCommandBuffer commandBuffer);

		// Deferred G-buffer pass
		void beginDeferredRenderPass(VkCommandBuffer commandBuffer);
		VkRenderPass getDeferredRenderPass() const { return deferredRenderFrameBuffer.renderPass; }
		// Blit G-buffer depth into swapchain depth so forward-rendered transparent objects depth-test against opaque geometry
		void blitGBufferDepthToSwapchain(VkCommandBuffer commandBuffer);

		// Multi-mip bloom: BLOOM_MIPS levels, each half the size of the previous
		void beginBloomDownsamplePass(VkCommandBuffer commandBuffer, int mip);
		void beginBloomUpsamplePass(VkCommandBuffer commandBuffer, int mip);
		VkRenderPass getBloomMipRenderPassClear(int mip) const { return bloomMips[mip].renderPassClear; }
		VkDescriptorImageInfo getBloomMipImageInfo(int mip) const
		{
			return {bloomMips[mip].sampler, bloomMips[mip].color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		}
		VkImage getBloomMipImage(int mip) const { return bloomMips[mip].color.image; }
		VkDeviceMemory getBloomMipMemory(int mip) const { return bloomMips[mip].color.mem; }
		// Store this VkDescriptorImageInfo in descriptor manager to include in the descriptor set
		VkSampler getOffscreenSampler() const { return offscreenPass.sampler; }
		VkImageView getOffscreenImageView() const { return offscreenPass.color.view; }
		VkDescriptorImageInfo getOffscreenImageInfo() const { return {offscreenPass.sampler, offscreenPass.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; }
		// Store this image as well
		VkImage getOffscreenImage() const { return offscreenPass.color.image; }
		// ... and this memory
		VkDeviceMemory getOffscreenMemory() const { return offscreenPass.color.mem; }
		VkRenderPass getOffscreenRenderPass() const { return offscreenPass.renderPass; }

		FrameBuffer getDeferredRenderFrameBuffer() const { return deferredRenderFrameBuffer; }

		// Radiosity buffer — HDR PBR lighting result, read by bloom and composite passes
		void beginRadiosityPass(VkCommandBuffer commandBuffer);
		VkRenderPass getRadiosityRenderPass() const { return radiosityBuffer.renderPass; }
		VkDescriptorImageInfo getRadiosityImageInfo() const
		{
			return {radiosityBuffer.sampler, radiosityBuffer.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		}
		VkImage getRadiosityImage() const { return radiosityBuffer.color.image; }
		VkDeviceMemory getRadiosityMemory() const { return radiosityBuffer.color.mem; }

		VkSampler getDRSampler() const { return deferredRenderFrameBuffer.sampler; }
		VkImageView getDRDepthView() const { return deferredRenderFrameBuffer.depth.view; }
		VkImageView getDRNormalView() const { return deferredRenderFrameBuffer.normal.view; }
		VkImageView getDRAlbedoView() const { return deferredRenderFrameBuffer.albedo.view; }
		VkImageView getDREmissionView() const { return deferredRenderFrameBuffer.emission.view; }

		// Shadow map — depth-only pass rendered from the directional light's perspective, one pass per cascade
		void beginShadowMapPass(VkCommandBuffer commandBuffer, uint32_t cascade);
		VkRenderPass getShadowMapRenderPass() const { return shadowMapBuffer.renderPass; }
		VkSampler getShadowMapSampler() const { return shadowMapBuffer.sampler; }
		VkImageView getShadowMapDepthView(uint32_t cascade) const { return shadowMapBuffer.depth[cascade].view; }

		// Returns true (and resets the flag) if the G-buffer and bloom mips were recreated
		// this frame (e.g. window resize). The caller must re-register descriptor entries.
		bool consumeGBufferRecreated()
		{
			bool v = gbufferRecreated;
			gbufferRecreated = false;
			return v;
		}

		void applyVsync(bool enabled)
		{
			BGLSwapChain::vsyncEnabled = enabled;
			recreateSwapChain();
		}

	private:
		uint32_t currentImageIndex = 0;
		int currentFrameIndex = 0;
		bool isFrameStarted = false;
		VkExtent2D gbufferExtent{};	   // size the G-buffer was last built at
		bool gbufferRecreated = false; // set when G-buffer is rebuilt due to resize

		// Renderer tasks
		void createCommandBuffers();
		void freeCommandBuffers();
		void recreateSwapChain();

		// Offscreen Render tasks
		void createOffscreenColorAttachment();
		void createOffscreenDepthsAttachment(VkFormat &depthsFormat);
		void createOffscreenAttachmentDescriptors(std::array<VkAttachmentDescription, 2> &descriptors, VkFormat &depthsFormat);
		void createOffscreenSubpassDependencies(std::array<VkSubpassDependency, 2> &dependencies);
		void createOffscreenFrameBuffer();

		void createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment *attachment, uint32_t width, uint32_t height);
		void prepareDeferredRenderFrameBuffer();
		void prepareBloomMips();
		void prepareRadiosityBuffer();
		void prepareShadowMapBuffer();
		void prepareTransparentPass();          // create the MRT + swapchain LOAD render passes (once)
		void buildTransparentFramebuffers();    // (re)build per-swapchain-image MRT framebuffers
		void destroyTransparentFramebuffers();
		void destroyDeferredFrameBuffer();
		void destroyBloomMips();
		void destroyRadiosityBuffer();

		BGLWindow &bglWindow;
		BGLDevice &bglDevice;
		std::unique_ptr<BGLSwapChain> bglSwapChain;
		std::vector<VkCommandBuffer> commandBuffers;

		OffscreenPass offscreenPass{};
		VkCommandBuffer deferredCommandBuffer;
		FrameBuffer deferredRenderFrameBuffer{};
		std::array<BloomBuffer, BLOOM_MIPS> bloomMips{};
		RadiosityBuffer radiosityBuffer{};
		ShadowMapBuffer shadowMapBuffer{};

		// Forward transparent pass target: radiosity color + opaque G-buffer depth (single full-screen FB)
		VkRenderPass transparentRenderPass = VK_NULL_HANDLE;
		VkFramebuffer transparentFramebuffer = VK_NULL_HANDLE;
	};

}