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

		// lightVP is this cascade's light view-projection (ubo.directionalLight.lightSpaceMatrix[cascadeIndex]);
		// used to frustum-cull casters that don't reach into this cascade's shadow volume.
		void renderShadowCasters(FrameInfo& frameInfo, uint32_t cascadeIndex, const glm::mat4& lightVP);

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
