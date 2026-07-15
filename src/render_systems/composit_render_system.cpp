#include "composit_render_system.hpp"

#include <iostream>
#include <vulkan/vulkan.h>

namespace bagel {

	CompositRenderSystem::CompositRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(CompositionPush) }
		, registry{ _registry }
		, descriptorManager{ _descriptorManager }
	{
		std::cout << "Creating Composition Render System\n";
		createPipeline(renderPass,
			"/shaders/deferred_lighting.vert.spv",
			"/shaders/deferred_lighting.frag.spv",
			BGLPipeline::setupFullScreenPipeline);
	}

	void CompositRenderSystem::render(FrameInfo& frameInfo)
	{
		pushParams.time = frameInfo.time;

		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		vkCmdPushConstants(
			frameInfo.commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(CompositionPush), &pushParams);

		vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
	}

} // namespace bagel
