#include "render_systems/bagel_threaded_render_system.hpp"
#include <iostream>
#include <vulkan/vulkan_core.h>
#include <cassert>

#include <exception>
#include "bagel_util.hpp"
#include "engine/bagel_engine_device.hpp"
namespace bagel
{
BGLThreadedRenderSystem::BGLThreadedRenderSystem(
    BGLDevice device,
    VkExtent2D extent,
    std::vector<VkDescriptorSetLayout> setLayouts,
    size_t pushConstantSize) : stopRequested(false)
{
    createPipelineLayout(setLayouts, pushConstantSize);
    
    thread = std::thread(&BGLThreadedRenderSystem::worker, this);
};
BGLThreadedRenderSystem::~BGLThreadedRenderSystem()
{
    assert(threadStopped && "stopThread() must be called in the inherited class destructor");
    stopThread(); // release-build safety net; no-op if the derived dtor already stopped it
    vkDeviceWaitIdle(BGLDevice::device());
    vkDestroyCommandPool(BGLDevice::device(), commandPool, nullptr);
    vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
    vkDestroyRenderPass(BGLDevice::device(), renderPass, nullptr);
};

void BGLThreadedRenderSystem::init(BGLDevice device, VkExtent2D extent)
{
    createRenderPass();
    createFrameBuffer(extent);
    createCommandPool(device);
    createCommandBuffer(device);
    initialized = true;
}
void BGLThreadedRenderSystem::stopThread()
{
    if (threadStopped) return; // idempotent — safe to call more than once
    // Signal the thread to stop
    stopRequested = true;
    
    // Wake up the thread if it's sleeping/waiting on a condition variable
    cv.notify_all();
    // BLOCK the main thread until the worker thread has completely finished executing.
    // If we don't do this, the class memory gets destroyed while the thread is still running!
    if (thread.joinable())
    {
        thread.join();
    }
    threadStopped = true;
}

void BGLThreadedRenderSystem::recreateFrameBuffer(VkExtent2D extent)
{
    destroyFrameBuffer();
    createFrameBuffer(extent);
}
void BGLThreadedRenderSystem::createCommandPool(BGLDevice &device)
{
    QueueFamilyIndices queueFamilyIndices = device.findPhysicalQueueFamilies();
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
    if (vkCreateCommandPool(device.device(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        // Runs on the main thread via init(); throwing here propagates normally.
        throw std::runtime_error("failed to create command pool!");
    }
}
void BGLThreadedRenderSystem::createCommandBuffer(BGLDevice &device)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device.device(), &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}
// Requires pushconstant typename
void BGLThreadedRenderSystem::createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts, size_t pushConstantSize)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = static_cast<uint32_t>(pushConstantSize);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (setLayouts.size() != 1)
        std::cout << "Creating pipeline with descriptorSetLayout count of " << setLayouts.size() << "\n";
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();

    if (pushConstantSize > 0)
    {
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    }
    else
    {
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;
    }

    if (vkCreatePipelineLayout(BGLDevice::device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create pipeline layout");
    }
}
// Default argument for PipelineConfigInfoModifier lives on the declaration in the header.
void BGLThreadedRenderSystem::createPipeline(VkRenderPass renderPass, const char *vertexShaderFilePath, const char *fragmentShaderFilePath, void (*PipelineConfigInfoModifier)(PipelineConfigInfo &))
{
    // assert(bglSwapChain != nullptr && "Cannot create pipeline before swapchain");
    assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

    PipelineConfigInfo pipelineConfig{};
    BGLPipeline::defaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout;
    if (PipelineConfigInfoModifier != nullptr)
        PipelineConfigInfoModifier(pipelineConfig);

    bglPipeline = std::make_unique<BGLPipeline>(
        util::enginePath(vertexShaderFilePath),
        util::enginePath(fragmentShaderFilePath),
        pipelineConfig);
}
void BGLThreadedRenderSystem::worker()
{
    while (true)
    {
        std::unique_lock lk(mutex);
        cv.wait(lk, [this]
                { return threadReady || stopRequested; });
        if (stopRequested)
            return; // kill thread for good
        std::cout << "Worker thread is processing data\n";
        threadReady = false;
        try
        {
            VkCommandBufferBeginInfo cbinfo = {};
            commandBufferBeginInfo(cbinfo);
            vkBeginCommandBuffer(commandBuffer, &cbinfo);
            VkRenderPassBeginInfo rpinfo = {};
            renderPassBeginInfo(rpinfo);
            vkCmdBeginRenderPass(commandBuffer, &rpinfo, VK_SUBPASS_CONTENTS_INLINE);
            bglPipeline->bind(commandBuffer);
            this->render(frameInfo);
            vkCmdEndRenderPass(commandBuffer);
            if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to record command buffer!");
            }
        }
        catch (...)
        {
            // Never let an exception escape the worker thread — that calls std::terminate().
            // Stash it; wait() rethrows it on the main thread.
            workerError = std::current_exception();
        }
        threadProcessed = true;
        std::cout << "Worker thread signals data processing completed\n";
        lk.unlock();
        cv.notify_one();
    }
}
void BGLThreadedRenderSystem::run(FrameInfo &_frameInfo)
{
    assert(initialized && "ThreadedRenderSystem must be intialized with init() before run()");
    assert(bglPipeline && "createPipeline() must be called before run()");
    {
        std::lock_guard lk(mutex);
        threadReady = true;
        frameInfo = &_frameInfo;
        std::cout << "main() signals data ready for processing\n";
    } // unlock called automatically here whatever happens
    cv.notify_one();
}
void BGLThreadedRenderSystem::wait()
{
    // wait for the worker
    {
        std::unique_lock lk(mutex);
        cv.wait(lk, [this]
                { return threadProcessed; });
        threadProcessed = false;
    }
    // Surface any exception the worker captured, on the main thread.
    if (workerError)
    {
        std::exception_ptr e = workerError;
        workerError = nullptr;
        std::rethrow_exception(e);
    }
}
} // namespace bagel