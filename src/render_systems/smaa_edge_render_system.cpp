#include "smaa_edge_render_system.hpp"
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <iostream>

namespace bagel {

	SmaaEdgeRenderSystem::SmaaEdgeRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(SmaaEdgePush) }
		, descriptorManager{ _descriptorManager }
	{
		std::cout << "Creating SMAA Edge Render System\n";
		// Full-screen triangle (deferred_lighting.vert) + luma edge detection. Same fixed-function
		// config as the composite/bloom post passes (no depth, no cull, single triangle).
		createPipeline(renderPass,
			"/shaders/deferred_lighting.vert.spv",
			"/shaders/smaa_edge.frag.spv",
			BGLPipeline::setupFullScreenPipeline);
	}

	void SmaaEdgeRenderSystem::render(FrameInfo& frameInfo, uint32_t inputHandle)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		SmaaEdgePush push{};
		push.inputHandle = inputHandle;
		push.threshold = edgeThreshold;
		push.localContrastAdapt = localConstrastAdapt;
		push.method = static_cast<uint32_t>(edgeMethod);
		vkCmdPushConstants(
			frameInfo.commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(SmaaEdgePush), &push);

		vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
	}

} // namespace bagel
