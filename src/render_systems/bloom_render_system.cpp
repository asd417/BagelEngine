#include "bloom_render_system.hpp"
#include "engine/bagel_engine_device.hpp"
#include "bagel_util.hpp"

#include <vulkan/vulkan.h>
#include <iostream>

namespace bagel {

	BloomRenderSystem::BloomRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& /*descriptorManager*/)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(BloomDownPush) } // push range covers both structs
	{
		std::cout << "Creating Bloom Render System\n";

		// Downsample pipeline: overwrite output, no blending
		createPipeline(renderPass,
			"/shaders/deferred_lighting.vert.spv",
			"/shaders/bloom_downsample.frag.spv",
			BGLPipeline::setupFullScreenPipeline);

		// Upsample pipeline: additive blending to accumulate into existing mip content
		PipelineConfigInfo upConfig{};
		BGLPipeline::defaultPipelineConfigInfo(upConfig);
		BGLPipeline::setupFullScreenAdditivePipeline(upConfig);
		upConfig.renderPass     = renderPass; // compatible with all bloom mip render passes
		upConfig.pipelineLayout = pipelineLayout;
		upsamplePipeline = std::make_unique<BGLPipeline>(
			util::enginePath("/shaders/deferred_lighting.vert.spv"),
			util::enginePath("/shaders/bloom_upsample.frag.spv"),
			upConfig);
	}

	void BloomRenderSystem::renderDownsample(FrameInfo& frameInfo, BloomDownPush& push)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1, &frameInfo.globalDescriptorSets,
			0, nullptr);
		vkCmdPushConstants(
			frameInfo.commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(BloomDownPush), &push);
		vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
	}

	void BloomRenderSystem::renderUpsample(FrameInfo& frameInfo, BloomUpPush& push)
	{
		upsamplePipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1, &frameInfo.globalDescriptorSets,
			0, nullptr);
		vkCmdPushConstants(
			frameInfo.commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(BloomUpPush), &push);
		vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
	}

} // namespace bagel
