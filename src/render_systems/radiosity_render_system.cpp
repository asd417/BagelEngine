#include "radiosity_render_system.hpp"

#include <iostream>
#include <vulkan/vulkan.h>

namespace bagel {

    RadiosityRenderSystem::RadiosityRenderSystem(
        VkRenderPass renderPass,
        std::vector<VkDescriptorSetLayout> setLayouts,
        std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager)
        : BGLRenderSystem{ renderPass, setLayouts, 0 }
        , descriptorManager{ _descriptorManager }
    {
        std::cout << "Creating Radiosity Render System\n";
        createPipeline(renderPass,
            "/shaders/deferred_lighting.vert.spv",
            "/shaders/radiosity.frag.spv",
            BGLPipeline::setupFullScreenPipeline);
    }

    void RadiosityRenderSystem::render(FrameInfo& frameInfo)
    {
        bglPipeline->bind(frameInfo.commandBuffer);
        vkCmdBindDescriptorSets(
            frameInfo.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0, 1,
            &frameInfo.globalDescriptorSets,
            0, nullptr);
        vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
    }

} // namespace bagel
