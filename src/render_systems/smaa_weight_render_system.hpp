#pragma once
#include <memory>
#include <vector>

#include "bagel_render_system.hpp"
#include "engine/bagel_descriptors.hpp"

#include "bagel_frame_info.hpp"

namespace bagel {

	// SMAA 1x — pass 2 of 3 (blending-weight calculation). Full-screen pass reading the edges
	// texture + the AreaTex/SearchTex LUTs (all by bindless handle) and writing the RGBA weights
	// target. Reuses the full-screen triangle vertex shader; see shaders/smaa_weight.frag.
	struct SmaaWeightPush {
		uint32_t edgesHandle  = 0; // RG edges from pass 1
		uint32_t areaHandle   = 0; // AreaTex LUT (RG8)
		uint32_t searchHandle = 0; // SearchTex LUT (R8)
	};

	class SmaaWeightRenderSystem : BGLRenderSystem {
	public:
		SmaaWeightRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager);

		// Call inside the weights render pass.
		void render(FrameInfo& frameInfo, uint32_t edgesHandle, uint32_t areaHandle, uint32_t searchHandle);

	private:
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
