#pragma once
#include <memory>
#include <vector>

#include "bagel_render_system.hpp"
#include "engine/bagel_pipeline.hpp"
#include "bagel_frame_info.hpp"
#include "engine/bagel_descriptors.hpp"

namespace bagel {

    class RadiosityRenderSystem : BGLRenderSystem {
    public:
        RadiosityRenderSystem(
            VkRenderPass renderPass,
            std::vector<VkDescriptorSetLayout> setLayouts,
            std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager);

        void render(FrameInfo& frameInfo);

    private:
        std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
    };

} // namespace bagel
