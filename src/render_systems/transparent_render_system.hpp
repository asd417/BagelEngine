#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_frame_info.hpp"
#include "bagel_render_system.hpp"
#include "engine/bagel_descriptors.hpp"

namespace bagel {

	// Must match the push block in transparent.vert / transparent.frag
	struct TransparentPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{ 1.0f };
		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform   = 0;
		uint32_t materialRowBase = 0; // skinBase + skinIndex*numSlots; skinTable row for this draw
		float emissionLux = 1.0f;
		float time = 0.0f;            // cumulative seconds, for animated ocean waves
	};

	// Forward, alpha-blended pass for transparent submeshes. Runs in the HDR radiosity pass after
	// the radiosity (deferred lighting) pass and before bloom/composite, blending linear HDR into
	// the radiosity buffer (composite tonemaps later). Depth-tests read-only against the opaque
	// G-buffer depth; does not write depth. The procedural ocean is split out into WaterRenderSystem,
	// drawn right after this in the same pass.
	class TransparentRenderSystem : BGLRenderSystem {
	public:
		TransparentRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager,
			entt::registry& registry);

		void renderEntities(FrameInfo& frameInfo);

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
