#pragma once

#include <memory>
#include <vector>

#include "engine/bagel_engine_swap_chain.hpp"
#include "entt.hpp"
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

#include "bagel_frame_info.hpp"
#include "engine/bagel_descriptors.hpp"
#include "render_systems/threaded/bagel_threaded_render_system.hpp"

namespace bagel::threaded
{
struct GBufferPushConstantData
{
    glm::mat4 modelMatrix{1.0f};
    glm::vec4 scale{1.0f};
    uint32_t BufferedTransformHandle = 0;
    uint32_t UsesBufferedTransform = 0;
    uint32_t materialRowBase = 0; // skinBase + skinIndex*numSlots; skinTable row for this draw
    float emissionLux = 1.0f;
    uint32_t fallbackAlbedoMap = 0;
};

class GBufferRenderSystem : public BGLThreadedRenderSystem
{
  public:
    // Pipelines and the descriptor set layout are supplied later, to init().
    GBufferRenderSystem(
        BGLDevice &device,
        BGLSwapChain &swapchain,
        std::unique_ptr<BGLBindlessDescriptorManager> const &_descriptorManager,
        entt::registry &_registry);
    ~GBufferRenderSystem();

  private:
    // Rendering backend
    void createSampler();
    void createRenderPass() override;
    void createFrameBuffer(VkExtent2D extent) override;
    void destroyFrameBufferAttachments() override;
    void beginRenderPass() override;
    void render(const FrameInfo *frameInfo) override;

    const VkFormat normalsFormat = VK_FORMAT_R16G16B16A16_UNORM;
    const VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;
    const VkFormat emissionFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthsFormat;

    VkSampler sampler = VK_NULL_HANDLE;
    VkExtent2D frameBufferExtent;
    FrameBufferAttachment FBAnormal, FBAalbedo, FBAemission, FBAdepth;

    // Behavior
    entt::registry &registry;
    std::unique_ptr<BGLBindlessDescriptorManager> const &descriptorManager;
};

} // namespace bagel::threaded
