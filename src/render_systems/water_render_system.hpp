#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "../bagel_model.hpp"

namespace bagel {

	// Must match the push block in water.vert / water.frag.
	struct WaterPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{ 1.0f };
		float time = 0.0f;            // cumulative seconds, for the animated ocean waves
	};

	// Procedural ocean/water pass. Draws the planets' transparent ocean submesh(es), split out of
	// TransparentRenderSystem so water can become its own depth-aware effect. Runs in the SAME HDR
	// pass as the transparents (radiosity buffer, depth-test read-only against the opaque G-buffer
	// depth, no depth write) but AFTER them — see the frame loop.
	//
	// TODO(pre/post-water transparents): right now ALL transparents draw before the water and the
	// water draws last. For correct submerged-vs-surface transparency, split the transparent queue
	// into a pre-water group (drawn before this system; becomes the refracted under-sea color the
	// water samples) and a post-water group (drawn after — atmosphere / glass over the surface).
	// Classify per object by whether it sits inside the ocean sphere (radius < sealevel).
	class WaterRenderSystem : BGLRenderSystem {
	public:
		WaterRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			entt::registry& registry);

		void renderEntities(FrameInfo& frameInfo);

	private:
		entt::registry& registry;
	};

} // namespace bagel
