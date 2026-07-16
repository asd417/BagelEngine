#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "bagel_frame_info.hpp"
#include "engine/bagel_pipeline.hpp"
#include "entt.hpp"
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

// Nested in bagel::threaded so FrameBufferAttachment and the render systems below do not
// collide with the same-named types in bagel (bagel_renderer.hpp, render_systems/*.hpp),
// which are still the ones the app builds against.
namespace bagel::threaded
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
        destroy();
    }
    void destroy()
    {
        if (view == VK_NULL_HANDLE)
            return;
        vkDestroyImageView(BGLDevice::device(), view, nullptr);
        vkDestroyImage(BGLDevice::device(), image, nullptr);
        vkFreeMemory(BGLDevice::device(), mem, nullptr);
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    }
};
class BGLThreadedRenderSystem
{
  public:
    struct PipelineDef
    {
        std::string vert = "";
        std::string frag = "";
        size_t pushConstantSize = 0;
        void (*PipelineConfigInfoModifier)(PipelineConfigInfo &) = nullptr;
    };
    void run(FrameInfo &frameInfo);
    void wait();
    // call on swapchain outdate if framebuffer attachment is dependent on swapchain
    void recreateFrameBuffer(VkExtent2D extent);
    void init(BGLDevice &device, VkDescriptorSetLayout setLayout, const std::vector<PipelineDef> &pipelines, VkExtent2D extent);

  private:
    void worker();

  protected:
    BGLThreadedRenderSystem(BGLDevice &device);
    ~BGLThreadedRenderSystem();

    BGLThreadedRenderSystem(const BGLThreadedRenderSystem &) = delete;
    BGLThreadedRenderSystem &operator=(const BGLThreadedRenderSystem &) = delete;
    void shutdown();

    virtual void createRenderPass() = 0;
    virtual void createFrameBuffer(VkExtent2D extent) = 0;
    // call this in inherited class destructor
    virtual void destroyFrameBufferAttachments() = 0;
    virtual void beginRenderPass() = 0;
    // if this was initialized with multiple render pipelines, it is the child's job to bind the correct pipelines in this function
    virtual void render(const FrameInfo *frameInfo) = 0;
    void destroyFrameBuffer();

    void createCommandPool(BGLDevice &device);
    void createCommandBuffer(BGLDevice &device);
    void createPipelines(VkDescriptorSetLayout setLayout, const std::vector<PipelineDef> &pipelines);
    void createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment *attachment, uint32_t width, uint32_t height);
    void stopThread();

    bool initialized = false;
    bool shutdownComplete = false;
    bool threadStopped = false;

    BGLDevice &bglDevice;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer frameBuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    uint32_t pipelineCount = 0;
    std::vector<std::unique_ptr<BGLPipeline>> bglPipelines = {};

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    std::exception_ptr workerError = nullptr;
    std::atomic<bool> stopRequested; // Safe for cross-thread reads/writes
    std::mutex mutex;
    std::condition_variable cv;
    const FrameInfo *frameInfo = nullptr; // borrowed; set each frame in run(), read by the worker
    bool threadReady = false;
    bool threadProcessed = false;
    // Always keep the thread as the VERY LAST member variable.
    // It must initialize after all synchronization variables are ready.
    std::thread thread;
};

} // namespace bagel::threaded