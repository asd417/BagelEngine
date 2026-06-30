#pragma once
#include <memory>
#include <vector>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"

namespace bagel {

	// SMAA 1x — pass 1 of 3 (luma edge detection). A full-screen post pass that reads the
	// composite render system's output (passed by bindless handle, like the bloom passes) and
	// writes an RG edges target consumed by the blending-weight pass. Reuses the full-screen
	// triangle vertex shader (deferred_lighting.vert); see shaders/smaa_edge.frag.
	//
	// Prerequisites for wiring (not done by this system):
	//   - The composite pass must render to a SAMPLEABLE color texture (today it draws straight
	//     to the swapchain), and that texture's bindless handle is passed to render().
	//   - BGLRenderer must own an edges render pass + RG8 target, CLEARED to 0 each frame
	//     (this system discards non-edge pixels), supplied here as `renderPass`.
	struct SmaaEdgePush {
		uint32_t inputHandle = 0; // bindless handle of the composite output to edge-detect
		float threshold = 0.05f;
		float localContrastAdapt = 2.0f;
		uint32_t method = 0;      // 0 = luma, 1 = color, 2 = depth
	};

	class SmaaEdgeRenderSystem : BGLRenderSystem {
	public:
		SmaaEdgeRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager);

		// Call inside the edges render pass. `inputHandle` is the composite output texture.
		void render(FrameInfo& frameInfo, uint32_t inputHandle);
		float edgeThreshold = cfg::kSmaaEdgeThreshold;
		float localConstrastAdapt = cfg::kSmaaLocalContrastAdapt;
		int   edgeMethod = cfg::kSmaaEdgeMethod; // 0 = luma, 1 = color, 2 = depth (see SmaaEdgePush::method)

	private:
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
