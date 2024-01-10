#pragma once

#include <glm/gtc/constants.hpp>

#include "bagel_window.hpp"
#include "bagel_engine_device.hpp"

#include "bagel_engine_swap_chain.hpp"

#include "bgl_model.hpp"

#include <cassert>
#include <memory>
#include <vector>

namespace bagel {
	class BGLRenderer {
		// Framebuffer for offscreen rendering
		struct FrameBufferAttachment {
			VkImage image;
			VkDeviceMemory mem;
			VkImageView view;
		};
	public:
		BGLRenderer(BGLWindow& w, BGLDevice& d);
		~BGLRenderer();

		BGLRenderer(const BGLRenderer&) = delete;
		BGLRenderer& operator=(const BGLRenderer&) = delete;
		
		VkCommandBuffer beginPrimaryCMD();
		void endPrimaryCMD();

		void beginSwapChainRenderPass(VkCommandBuffer commandBuffer);
		void endCurrentRenderPass(VkCommandBuffer commandBuffer);

		bool isFrameInProgress() const { return isFrameStarted; }
		VkCommandBuffer getCurrentCommandBuffer() const {
			assert(isFrameStarted && "Cannot get command buffer when frame not in progress");
			return commandBuffers[currentFrameIndex];
		}

		size_t getSwapChainImageCount() const { return bglSwapChain->imageCount(); }
		VkRenderPass getSwapChainRenderPass() const { return bglSwapChain->getRenderPass(); }
		float getAspectRatio() const { return bglSwapChain->extentAspectRatio(); }
		VkExtent2D getExtent() const { return bglSwapChain->getSwapChainExtent(); };

		int getFrameIndex() const {
			assert(isFrameStarted && "Cannot get frame index when frame not in progress");
			return currentFrameIndex;
		}

		//Offscreen Render tasks
		void setUpOffScreenRenderPass(uint32_t textureWidth, uint32_t textureHeight);
		void createOffScreenRenderPass(uint32_t textureWidth, uint32_t textureHeight);
		void beginOffScreenRenderPass(VkCommandBuffer commandBuffer);
		// Store this ImageView in descriptor manager to include in the descriptor set
		VkImageView getOffscreenRenderImageView() const { return offscreenPass.color.view; };
		// Store this Sampler in descriptor manager to include in the descriptor set
		VkSampler getOffscreenRenderSampler() const { return offscreenPass.sampler; };
		VkRenderPass getOffscreenRenderPass() const { return offscreenPass.renderPass; };

	private:
		struct OffscreenPass {
			uint32_t width, height;
			VkFramebuffer frameBuffer;
			FrameBufferAttachment color, depth;
			VkRenderPass renderPass;
			VkSampler sampler;
			uint32_t renderTargetHandle;
		} offscreenPass;

		uint32_t currentImageIndex = 0;
		int currentFrameIndex = 0; //Between 0 and Max_Frames_In_Flight, not tied to image index
		bool isFrameStarted = false;

		//Renderer tasks
		void createCommandBuffers();
		void freeCommandBuffers();
		void recreateSwapChain();

		//Offscreen Render tasks
		void createOffscreenColorAttachment();
		void createOffscreenDepthsAttachment(VkFormat& depthsFormat);
		void createOffscreenAttachmentDescriptors(std::array<VkAttachmentDescription,2>& descriptors, VkFormat& depthsFormat);
		void createOffscreenSubpassDependencies(std::array<VkSubpassDependency, 2>& dependencies);
		void createOffscreenFrameBuffer();

		BGLWindow& bglWindow;
		BGLDevice& bglDevice;
		std::unique_ptr<BGLSwapChain> bglSwapChain;
		std::vector<VkCommandBuffer> commandBuffers;
		//VkCommandBuffer secondaryCommandBuffer;
	};

}