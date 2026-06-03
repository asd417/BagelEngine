#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/glm.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "gbuffer_render_system.hpp" // reuses GBufferPushConstantData

namespace bagel {

	class TransparentRenderSystem : BGLRenderSystem {
	public:
		TransparentRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry);

		void render(FrameInfo& frameInfo);

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
