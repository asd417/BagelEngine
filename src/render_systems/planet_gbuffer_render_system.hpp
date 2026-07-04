#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/glm.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"

namespace bagel {

	struct PlanetGBufferPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 centerRadius{ 0.0f }; // xyz = planet center (world), w = base radius
		glm::vec4 noiseF{ 0.0f };       // amplitude, frequency, lacunarity, gain
		glm::vec4 noiseF2{ 0.0f };      // sealevel, octaves, seed, gradient-epsilon
	};                                  // 112 bytes total — within the guaranteed 128-byte push-constant minimum

	// Draws PlanetComponent entities into the deferred gbuffer with a dedicated
	// procedural pipeline (surface reconstructed per-fragment from noise(dir), so
	// shading is invariant to the LOD tessellation). Runs inside the existing
	// gbuffer pass; the standard GBufferRenderSystem skips these entities.
	class PlanetGBufferRenderSystem : BGLRenderSystem {
	public:
		PlanetGBufferRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			entt::registry& _registry);

		void renderEntities(FrameInfo& frameInfo);

	private:
		entt::registry& registry;
	};

} // namespace bagel
