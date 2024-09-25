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
	struct FrameBufferAttachment {
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format;
		const FrameBufferAttachment operator=(const FrameBufferAttachment&) = delete;
		~FrameBufferAttachment() {
			std::cout << "Destroying FrameBufferAttachment\n";
			vkDestroyImageView(BGLDevice::device(), view, nullptr);
			vkDestroyImage(BGLDevice::device(), image, nullptr);
			vkFreeMemory(BGLDevice::device(), mem, nullptr);
			std::cout << "Finished Destroying FrameBufferAttachment\n";
		}
	};

	struct FrameBuffer {
		int32_t width, height;
		VkFramebuffer frameBuffer;
		// One attachment for every component required for a deferred rendering setup
		FrameBufferAttachment position, normal, albedo;
		FrameBufferAttachment depth;
		VkRenderPass renderPass;
		VkSampler sampler;
		const FrameBuffer operator=(const FrameBuffer&) = delete;
		~FrameBuffer() {
			std::cout << "Destroying FrameBuffer\n";
			vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
			vkDestroyRenderPass(BGLDevice::device(), renderPass, nullptr);
			vkDestroySampler(BGLDevice::device(), sampler, nullptr);
			std::cout << "Finished Destroying FrameBuffer\n";
		}
	};

	struct OffscreenPass {
		uint32_t width, height;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment color{};
		FrameBufferAttachment depth{};
		VkRenderPass renderPass;
		VkSampler sampler;
		uint32_t renderTargetHandle;
		VkDescriptorImageInfo colorImageInfo;
		~OffscreenPass() {
			std::cout << "Destroying OffscreenPass\n";
			vkDestroyRenderPass(BGLDevice::device(), renderPass, nullptr);
			vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
			std::cout << "Finished Destroying OffscreenPass\n";
		}
	};

	class BGLRenderer {
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
		VkExtent2D getExtent() const { return bglSwapChain->getSwapChainExtent(); }

		int getFrameIndex() const {
			assert(isFrameStarted && "Cannot get frame index when frame not in progress");
			return currentFrameIndex;
		}

		//Offscreen Render tasks
		void setUpOffScreenRenderPass(uint32_t textureWidth, uint32_t textureHeight);
		void createOffScreenRenderPass(uint32_t textureWidth, uint32_t textureHeight);
		void beginOffScreenRenderPass(VkCommandBuffer commandBuffer);
		// Store this VkDescriptorImageInfo in descriptor manager to include in the descriptor set
		VkSampler getOffscreenSampler() const{ return offscreenPass.sampler; }
		VkImageView getOffscreenImageView() const { return offscreenPass.color.view; }
		VkDescriptorImageInfo getOffscreenImageInfo() const { return { offscreenPass.sampler, offscreenPass.color.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; }
		// Store this image as well
		VkImage getOffscreenImage() const { return offscreenPass.color.image; }
		// ... and this memory
		VkDeviceMemory getOffscreenMemory() const { return offscreenPass.color.mem; }
		VkRenderPass getOffscreenRenderPass() const { return offscreenPass.renderPass; }

		FrameBuffer getDeferredRenderFrameBuffer() const { return deferredRenderFrameBuffer; }

		VkSampler getDRSampler() const { return deferredRenderFrameBuffer.sampler; }
		VkImageView getDRPositionView() const { return deferredRenderFrameBuffer.position.view; }
		VkImageView getDRNormalView() const { return deferredRenderFrameBuffer.normal.view; }
		VkImageView getDRAlbedoView() const { return deferredRenderFrameBuffer.albedo.view; }
		
	private:

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

		void createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment* attachment);
		void prepareDeferredRenderFrameBuffer();
		

		BGLWindow& bglWindow;
		BGLDevice& bglDevice;
		std::unique_ptr<BGLSwapChain> bglSwapChain;
		std::vector<VkCommandBuffer> commandBuffers;

		OffscreenPass offscreenPass{};
		VkCommandBuffer deferredCommandBuffer;
		FrameBuffer deferredRenderFrameBuffer{};
	};

}