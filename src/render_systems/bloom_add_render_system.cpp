#include "bloom_add_render_system.hpp"
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <iostream>

namespace bagel {

	BloomAddRenderSystem::BloomAddRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(BloomAddPush) }
		, descriptorManager{ _descriptorManager }
		, registry{ _registry }
	{
		std::cout << "Creating BloomAdd Render System\n";
		createPipeline(renderPass,
			"/shaders/deferred_lighting.vert.spv", // reuse the full-screen triangle vertex shader
			"/shaders/bloom_add.frag.spv",
			BGLPipeline::setupFullScreenAdditivePipeline);
	}

	void BloomAddRenderSystem::render(FrameInfo& frameInfo)
	{
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
			0, sizeof(BloomAddPush), &pushParams);

		vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
	}

} // namespace bagel
