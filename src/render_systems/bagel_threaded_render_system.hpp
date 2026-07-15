#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>
#include "engine/bagel_pipeline.hpp"
#include "bagel_frame_info.hpp"
namespace bagel {

class BGLThreadedRenderSystem
{
    public:
      void run(FrameInfo& frameInfo);
      void wait();
      void recreateFrameBuffer(VkExtent2D extent);
    private:
      void worker();
	protected:
      BGLThreadedRenderSystem(
            BGLDevice device,
			VkExtent2D extent, 
			std::vector<VkDescriptorSetLayout> setLayouts, 
			size_t pushConstantSize);
		~BGLThreadedRenderSystem();

		BGLThreadedRenderSystem(const BGLThreadedRenderSystem&) = delete;
		BGLThreadedRenderSystem& operator=(const BGLThreadedRenderSystem&) = delete;
        void init(BGLDevice device, VkExtent2D extent);

        virtual void createRenderPass() = 0;
        virtual void createFrameBuffer(VkExtent2D extent) = 0;
        virtual void destroyFrameBuffer() = 0;
        virtual void commandBufferBeginInfo(VkCommandBufferBeginInfo& info) = 0;
        virtual void renderPassBeginInfo(VkRenderPassBeginInfo &info) = 0;
        
        void createCommandPool(BGLDevice& device);
        void createCommandBuffer(BGLDevice &device);
		void createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts, size_t pushConstantSize);
		void createPipeline(VkRenderPass renderPass, const char* vertexShaderFilePath, const char* FragmentShaderFilePath, void (*PipelineConfigInfoModifier)(PipelineConfigInfo&) = nullptr );

        virtual void render(const FrameInfo *frameInfo) = 0;

        void stopThread();

        bool initialized = false;
        bool threadStopped = false;

        VkRenderPass renderPass;
        VkFramebuffer frameBuffer;
		std::unique_ptr<BGLPipeline> bglPipeline;
        VkPipelineLayout pipelineLayout;
        VkCommandPool commandPool;
        VkCommandBuffer commandBuffer;

        std::exception_ptr workerError = nullptr;
        std::atomic<bool> stopRequested; // Safe for cross-thread reads/writes
        std::mutex mutex;
        std::condition_variable cv;
        const FrameInfo* frameInfo = nullptr; // borrowed; set each frame in run(), read by the worker
        bool threadReady = false;
        bool threadProcessed = false;
        // Always keep the thread as the VERY LAST member variable.
        // It must initialize after all synchronization variables are ready.
        std::thread thread;
	};

}