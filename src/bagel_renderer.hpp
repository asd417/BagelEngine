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
	public:
		BGLRenderer(BGLWindow& w, BGLDevice& d);
		~BGLRenderer();

		BGLRenderer(const BGLRenderer&) = delete;
		BGLRenderer& operator=(const BGLRenderer&) = delete;
		
		VkCommandBuffer beginPrimaryCMD();
		void endPrimaryCMD();

		VkCommandBuffer beginSecondaryCMD();
		void endSecondaryCMD();

		void beginSwapChainRenderPass(VkCommandBuffer commandBuffer);
		void endSwapChainRenderPass(VkCommandBuffer commandBuffer);

		bool isFrameInProgress() const { return isFrameStarted; }
		VkCommandBuffer getCurrentCommandBuffer() const {
			assert(isFrameStarted && "Cannot get command buffer when frame not in progress");
			return commandBuffers[currentFrameIndex];
		}

		size_t getSwapChainImageCount() const { return bglSwapChain->imageCount(); }
		VkRenderPass getSwapChainRenderPass() const { return bglSwapChain->getRenderPass(); }
		float getAspectRatio() const { return bglSwapChain->extentAspectRatio(); }

		int getFrameIndex() const {
			assert(isFrameStarted && "Cannot get frame index when frame not in progress");
			return currentFrameIndex;
		}
	private:

		uint32_t currentImageIndex = 0;
		int currentFrameIndex = 0; //Between 0 and Max_Frames_In_Flight, not tied to image index
		bool isFrameStarted = false;
		//Renderer tasks
		void createCommandBuffers();
		void freeCommandBuffers();
		void recreateSwapChain();

		BGLWindow& bglWindow;
		BGLDevice& bglDevice;
		std::unique_ptr<BGLSwapChain> bglSwapChain;
		std::vector<VkCommandBuffer> commandBuffers;
		VkCommandBuffer secondaryCommandBuffer;
	};

}