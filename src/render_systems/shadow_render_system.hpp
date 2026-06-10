#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/glm.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"

namespace bagel {

	struct ShadowPushData {
		glm::mat4 modelMatrix{ 1.0f };
		uint32_t  BufferedTransformHandle = 0;
		uint32_t  UsesBufferedTransform   = 0;
		uint32_t  cascadeIndex            = 0;
	};

	class ShadowRenderSystem : BGLRenderSystem {
	public:
		ShadowRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager,
			entt::registry& registry);

		void renderShadowCasters(FrameInfo& frameInfo, uint32_t cascadeIndex);

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
