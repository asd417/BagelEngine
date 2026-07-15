#pragma once
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_frame_info.hpp"
#include "bagel_render_system.hpp"
#include "engine/bagel_descriptors.hpp"

namespace bagel {

	// Push layout is binary-compatible with GBufferPushConstantData: the two buffered-transform
	// slots (offsets 80/84) are repurposed as skinVertexBase/animBaseOffset, so this pass reuses
	// gbuffer_fill.frag — only the vertex shader (skinned_gbuffer.vert) differs.
	struct SkinnedGBufferPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{ 1.0f };
		uint32_t  skinVertexBase    = 0; // base into the per-vertex skin-influence SSBO
		uint32_t  animBaseOffset    = 0; // base into the joint palette SSBO for this frame
		uint32_t  materialRowBase   = 0; // skinBase + skinIndex*numSlots
		float     emissionLux       = 1.0f;
		uint32_t  fallbackAlbedoMap = 0;
	};

	// Deferred pass for skeletally-animated models. Draws into the SAME G-buffer as the static
	// GBufferRenderSystem (shared render pass), per-entity (no buffered/instanced transform),
	// reading the skin-influence + joint-palette SSBOs. Advances each entity's animation time.
	class AnimatedGBufferRenderSystem : BGLRenderSystem {
	public:
		AnimatedGBufferRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry);

		void renderEntities(FrameInfo& frameInfo);

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
