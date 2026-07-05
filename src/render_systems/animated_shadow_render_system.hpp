#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/glm.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"

namespace bagel {

	// Push for the skinned shadow caster. Offsets 64/68 (the buffered-transform slots in the
	// static ShadowPushData) are repurposed as skinVertexBase/animBaseOffset; cascadeIndex
	// stays at 72, so this reuses shadow.frag and the shadow-map pipeline config.
	struct SkinnedShadowPushData {
		glm::mat4 modelMatrix{ 1.0f };
		uint32_t  skinVertexBase = 0;
		uint32_t  animBaseOffset = 0;
		uint32_t  cascadeIndex   = 0;
	};

	// Renders skeletally-animated shadow casters into the shadow map with the SAME deformed
	// pose the g-buffer pass uses, so the shadow silhouette tracks the animation instead of
	// the bind pose. Runs alongside ShadowRenderSystem (which now skips skinned models).
	class SkinnedShadowRenderSystem : BGLRenderSystem {
	public:
		SkinnedShadowRenderSystem(
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
