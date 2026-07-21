#pragma once
#include <vector>

#include "entt.hpp"
#include <glm/glm.hpp>

#include "bagel_frame_info.hpp"
#include "bagel_render_system.hpp"

namespace bagel {

	// Draws PlanetComponent entities into the deferred gbuffer. Modeled on GBufferRenderSystem and
	// reusing the same gbuffer_fill pipeline/shaders and push constant, so the generated planet mesh
	// (indexed, with analytic normals) shades identically to any other model. Kept as its own system
	// so planets can later diverge to a dedicated terrain pipeline; the standard GBufferRenderSystem
	// skips planet entities so they are not drawn twice.
	class PlanetRenderSystem : BGLRenderSystem {
	public:
		PlanetRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			entt::registry& _registry);

		void renderEntities(FrameInfo& frameInfo);

	private:
		entt::registry& registry;
	};

} // namespace bagel
