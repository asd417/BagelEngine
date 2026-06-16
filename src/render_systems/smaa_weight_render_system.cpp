#include "smaa_weight_render_system.hpp"
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <iostream>

namespace bagel {

	SmaaWeightRenderSystem::SmaaWeightRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(SmaaWeightPush) }
		, descriptorManager{ _descriptorManager }
	{
		std::cout << "Creating SMAA Weight Render System\n";
		createPipeline(renderPass,
			"/shaders/deferred_lighting.vert.spv", // full-screen triangle
			"/shaders/smaa_weight.frag.spv",
			BGLPipeline::setupFullScreenPipeline);
	}

	void SmaaWeightRenderSystem::render(FrameInfo& frameInfo, uint32_t edgesHandle, uint32_t areaHandle, uint32_t searchHandle)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		SmaaWeightPush push{};
		push.edgesHandle  = edgesHandle;
		push.areaHandle   = areaHandle;
		push.searchHandle = searchHandle;
		vkCmdPushConstants(
			frameInfo.commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(SmaaWeightPush), &push);

		vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
	}

} // namespace bagel
