#include "render_systems/threaded/bagel_threaded_render_system.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

#include "bagel_util.hpp"
#include "engine/bagel_engine_device.hpp"
#include "engine/bagel_pipeline.hpp"
#include <exception>

namespace bagel::threaded
{
BGLThreadedRenderSystem::BGLThreadedRenderSystem(BGLDevice &device) : bglDevice(device), stopRequested(false)
{
    thread = std::thread(&BGLThreadedRenderSystem::worker, this);
};
BGLThreadedRenderSystem::~BGLThreadedRenderSystem()
{
    assert(shutdownComplete && "shutdown() must be called in the inherited class destructor");
    // Release-build safety net: without this, a subclass that forgets shutdown() reaches
    // ~std::thread while still joinable, which is an unconditional std::terminate().
    // Cannot save the derived attachments — they are already gone by now — but a leak beats a crash.
    shutdown();
    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(BGLDevice::device(), commandPool, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(BGLDevice::device(), pipelineLayout, nullptr);
    }
    destroyFrameBuffer();
    if (renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(BGLDevice::device(), renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
};
void BGLThreadedRenderSystem::init(BGLDevice &device, VkDescriptorSetLayout setLayout, const std::vector<PipelineDef> &pipelines, VkExtent2D extent)
{
    createFrameBuffer(extent);
    createCommandPool(device);
    createCommandBuffer(device);
    createPipelines(setLayout, pipelines);
    initialized = true;
}
void BGLThreadedRenderSystem::shutdown()
{
    if (shutdownComplete)
        return;                            // idempotent — the base dtor calls this again as a safety net
    stopThread();                          // join
    vkDeviceWaitIdle(BGLDevice::device()); // nothing may be freed while the GPU still reads it
    shutdownComplete = true;
}
void BGLThreadedRenderSystem::recreateFrameBuffer(VkExtent2D extent)
{
    destroyFrameBuffer();
    destroyFrameBufferAttachments();
    createFrameBuffer(extent);
}
void BGLThreadedRenderSystem::destroyFrameBuffer()
{
    if (frameBuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
        frameBuffer = VK_NULL_HANDLE;
    }
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
// Default argument for PipelineConfigInfoModifier lives on the declaration in the header.
void BGLThreadedRenderSystem::createPipelines(VkDescriptorSetLayout setLayout, const std::vector<PipelineDef> &pipelines)
{
    bglPipelines.resize(pipelines.size());
    size_t maxPushSize = 0;
    for (int i = 0; i < static_cast<uint32_t>(pipelines.size()); i++)
    {
        size_t s = pipelines[i].pushConstantSize;
        maxPushSize = s > maxPushSize ? s : maxPushSize;
    }
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = static_cast<uint32_t>(maxPushSize);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout;

    if (maxPushSize > 0)
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
    for (int i = 0; i < static_cast<uint32_t>(pipelines.size()); i++)
    {
        PipelineConfigInfo pipelineConfig{};
        BGLPipeline::defaultPipelineConfigInfo(pipelineConfig);
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;
        if (pipelines[i].PipelineConfigInfoModifier != nullptr)
            pipelines[i].PipelineConfigInfoModifier(pipelineConfig);
        bglPipelines[i] = std::make_unique<BGLPipeline>(
            util::enginePath(pipelines[i].vert.c_str()),
            util::enginePath(pipelines[i].frag.c_str()),
            pipelineConfig);
    }
    pipelineCount = static_cast<uint32_t>(pipelines.size());
}

void BGLThreadedRenderSystem::createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment *attachment, uint32_t width, uint32_t height)
{
    VkImageAspectFlags aspectMask = 0;
    // VkImageLayout imageLayout;

    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    {
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        // imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format >= VK_FORMAT_D16_UNORM_S8_UINT)
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        // imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
void BGLThreadedRenderSystem::stopThread()
{
    if (threadStopped)
        return; // idempotent — safe to call more than once
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
void BGLThreadedRenderSystem::worker()
{
    while (true)
    {
        std::unique_lock lk(mutex);
        cv.wait(lk, [this]
                { return threadReady || stopRequested; });
        if (stopRequested)
            return; // kill thread for good
        threadReady = false;
        try
        {
            VkCommandBufferBeginInfo cbinfo = {};
            cbinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cbinfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            vkBeginCommandBuffer(commandBuffer, &cbinfo);
            beginRenderPass();
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
        lk.unlock();
        cv.notify_one();
    }
}
void BGLThreadedRenderSystem::run(FrameInfo &_frameInfo)
{
    assert(initialized && "ThreadedRenderSystem must be intialized with init() before run()");
    assert(pipelineCount != 0 && "createPipelines() must be called before run()");
    {
        std::lock_guard lk(mutex);
        threadReady = true;
        frameInfo = &_frameInfo;
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
} // namespace bagel::threaded