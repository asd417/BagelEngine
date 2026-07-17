#include "render_systems/new/bagel_render_system.hpp"
#include <cassert>
#include <iostream> // VK_CHECK expands to std::cout
#include <stdexcept>
#include <vulkan/vulkan_core.h>

#include "bagel_util.hpp"
#include "engine/bagel_engine_device.hpp"
#include "engine/bagel_pipeline.hpp"

namespace bagel::newdesign
{
BGLRenderSystem::BGLRenderSystem(BGLDevice &device) : bglDevice(device) {};
BGLRenderSystem::~BGLRenderSystem()
{
    assert(shutdownComplete && "shutdown() must be called in the inherited class destructor");
    // Release-build safety net for a subclass that forgets shutdown(). It cannot save the
    // derived attachments — those were freed without a wait when the derived destructor ran —
    // but it still gets the GPU idle before the base tears down its own objects below.
    shutdown();
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
void BGLRenderSystem::init(VkDescriptorSetLayout setLayout, const std::vector<PipelineDef> &pipelines, VkExtent2D extent)
{
    createRenderPass();
    createFrameBuffer(extent);
    createPipelines(setLayout, pipelines);
    initialized = true;
}
void BGLRenderSystem::shutdown()
{
    if (shutdownComplete)
        return;                            // idempotent — the base dtor calls this again as a safety net
    vkDeviceWaitIdle(BGLDevice::device()); // nothing may be freed while the GPU still reads it
    shutdownComplete = true;
}
void BGLRenderSystem::recreateFrameBuffer(VkExtent2D extent)
{
    destroyFrameBuffer();
    destroyFrameBufferAttachments();
    createFrameBuffer(extent);
}
void BGLRenderSystem::destroyFrameBuffer()
{
    if (frameBuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(BGLDevice::device(), frameBuffer, nullptr);
        frameBuffer = VK_NULL_HANDLE;
    }
}
void BGLRenderSystem::createPipelines(VkDescriptorSetLayout setLayout, const std::vector<PipelineDef> &pipelines)
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

void BGLRenderSystem::createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment *attachment, uint32_t width, uint32_t height)
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
void BGLRenderSystem::run(const FrameInfo &frameInfo)
{
    assert(initialized && "BGLRenderSystem must be initialized with init() before run()");
    assert(pipelineCount != 0 && "createPipelines() must be called before run()");
    beginRenderPass(frameInfo.commandBuffer);
    this->render(frameInfo);
    vkCmdEndRenderPass(frameInfo.commandBuffer);
}
} // namespace bagel::newdesign