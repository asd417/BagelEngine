#include "smaa_neighborhood_render_system.hpp"
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <iostream>

namespace bagel {

	SmaaNeighborhoodRenderSystem::SmaaNeighborhoodRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(SmaaNeighborhoodPush) }
		, descriptorManager{ _descriptorManager }
	{
		std::cout << "Creating SMAA Neighborhood Render System\n";
		createPipeline(renderPass,
			"/shaders/deferred_lighting.vert.spv", // full-screen triangle
			"/shaders/smaa_neighborhood.frag.spv",
			BGLPipeline::setupFullScreenPipeline);
	}

	void SmaaNeighborhoodRenderSystem::render(FrameInfo& frameInfo, uint32_t colorHandle, uint32_t weightHandle, bool enabled)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		SmaaNeighborhoodPush push{};
		push.colorHandle  = colorHandle;
		push.weightHandle = weightHandle;
		push.enabled      = enabled ? 1u : 0u;
		vkCmdPushConstants(
			frameInfo.commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(SmaaNeighborhoodPush), &push);

		vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
	}

} // namespace bagel
