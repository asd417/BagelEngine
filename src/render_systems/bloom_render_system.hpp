#pragma once
#include <memory>
#include <vector>

#include "bagel_render_system.hpp"
#include "engine/bagel_pipeline.hpp"
#include "bagel_frame_info.hpp"

namespace bagel {

	// Push constants for the downsample pass
	struct BloomDownPush {
		float    threshold   = 0.0f; // luminance cutoff; 0 = no threshold (middle passes)
		float    intensity   = 1.0f; // output brightness scale
		uint32_t inputHandle = 0;    // 0 = sample gEmission, else samplerColor[inputHandle]
	};

	// Push constants for the upsample pass
	struct BloomUpPush {
		float    filterRadius = 1.0f; // tent kernel radius in texels of the source mip
		uint32_t inputHandle  = 0;    // bindless handle of the smaller mip to upsample
		float    weight       = 1.0f; // contribution weight — decreases for coarser mips
	};

	class BloomRenderSystem : BGLRenderSystem {
	public:
		BloomRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager);

		// Render a downsample pass — call inside beginBloomDownsamplePass / endCurrentRenderPass
		void renderDownsample(FrameInfo& frameInfo, BloomDownPush& push);
		// Render an upsample pass  — call inside beginBloomUpsamplePass  / endCurrentRenderPass
		void renderUpsample(FrameInfo& frameInfo, BloomUpPush& push);

	private:
		std::unique_ptr<BGLPipeline> upsamplePipeline;
	};

} // namespace bagel
