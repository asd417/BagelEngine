#pragma once
#include <memory>
#include <vector>

#include "bagel_render_system.hpp"
#include "engine/bagel_pipeline.hpp"
#include "bagel_frame_info.hpp"

namespace bagel {

	// SMAA 1x — pass 3 of 3 (neighborhood blending). Full-screen pass that reads the composite LDR
	// color + the blend weights and writes the anti-aliased result to the swapchain (it doubles as
	// the present pass; overlays draw on top after it). `enabled` = 0 passes the color through.
	struct SmaaNeighborhoodPush {
		uint32_t colorHandle  = 0; // composite LDR image
		uint32_t weightHandle = 0; // weights from pass 2
		uint32_t enabled      = 1; // 0 = passthrough, 1 = blend
	};

	class SmaaNeighborhoodRenderSystem : BGLRenderSystem {
	public:
		SmaaNeighborhoodRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager);

		// Call inside the swapchain render pass, before overlays.
		void render(FrameInfo& frameInfo, uint32_t colorHandle, uint32_t weightHandle, bool enabled);

	private:
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
