#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "bagel_frame_info.hpp"
#include "engine/bagel_pipeline.hpp"
#include "entt.hpp"
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

// Nested in bagel::newdesign so FrameBufferAttachment and the render systems below do not
// collide with the same-named types in bagel (bagel_renderer.hpp, render_systems/*.hpp),
// which are still the ones the app builds against.
namespace bagel::newdesign
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
class BGLRenderSystem
{
  public:
    struct PipelineDef
    {
        std::string vert = "";
        std::string frag = "";
        size_t pushConstantSize = 0;
        void (*PipelineConfigInfoModifier)(PipelineConfigInfo &) = nullptr;
    };
    void run(const FrameInfo &frameInfo);
    // call on swapchain outdate if framebuffer attachment is dependent on swapchain
    void recreateFrameBuffer(VkExtent2D extent);
    void init(VkDescriptorSetLayout setLayout, const std::vector<PipelineDef> &pipelines, VkExtent2D extent);

  protected:
    BGLRenderSystem(BGLDevice &device);
    ~BGLRenderSystem();

    BGLRenderSystem(const BGLRenderSystem &) = delete;
    BGLRenderSystem &operator=(const BGLRenderSystem &) = delete;
    // Must be the first statement of the inherited class destructor: the derived destructor
    // frees its own attachments before the base destructor ever runs, so the GPU has to be
    // idle by then. A wait in ~BGLRenderSystem would already be too late for those.
    void shutdown();

    virtual void createRenderPass() = 0;
    virtual void createFrameBuffer(VkExtent2D extent) = 0;
    // call this in inherited class destructor
    virtual void destroyFrameBufferAttachments() = 0;
    virtual void beginRenderPass(VkCommandBuffer commandbuffer) = 0;
    // if this was initialized with multiple render pipelines, it is the child's job to bind the correct pipelines in this function
    virtual void render(const FrameInfo &frameInfo) = 0;
    void destroyFrameBuffer();

    void createPipelines(VkDescriptorSetLayout setLayout, const std::vector<PipelineDef> &pipelines);
    void createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment *attachment, uint32_t width, uint32_t height);

    bool initialized = false;
    bool shutdownComplete = false;
    BGLDevice &bglDevice;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer frameBuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    uint32_t pipelineCount = 0;
    std::vector<std::unique_ptr<BGLPipeline>> bglPipelines = {};
};

} // namespace bagel::newdesign