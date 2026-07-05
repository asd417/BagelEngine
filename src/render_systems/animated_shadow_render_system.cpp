#include "skinned_shadow_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

	SkinnedShadowRenderSystem::SkinnedShadowRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(SkinnedShadowPushData) }
		, descriptorManager{ _descriptorManager }
		, registry{ _registry }
	{
		std::cout << "Creating Skinned Shadow Render System\n";
		// Reuses the depth-only shadow fragment shader + shadow-map pipeline config.
		createPipeline(renderPass,
			"/shaders/shadow_skinned.vert.spv",
			"/shaders/shadow.frag.spv",
			BGLPipeline::setupShadowMapPipeline);
	}

	void SkinnedShadowRenderSystem::renderShadowCasters(FrameInfo& frameInfo, uint32_t cascadeIndex)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize offsets[] = { 0 };

		// Animation time is advanced once per frame in the engine loop; read-only here so the
		// shadow pose matches the g-buffer pose exactly.
		auto view = registry.view<TransformComponent, ModelComponent, AnimationComponent>();
		for (auto [entity, transform, model, anim] : view.each()) {
			if (!model.isSkinned) continue;

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);
			if (model.indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			SkinnedShadowPushData push{};
			push.modelMatrix    = transform.getMat4();
			push.skinVertexBase = model.skinVertexBase;
			push.animBaseOffset = anim.animBaseOffset();
			push.cascadeIndex   = cascadeIndex;
			vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(SkinnedShadowPushData), &push);

			// Whole mesh casts (all submeshes, opaque depth).
			for (uint32_t i = 0; i < model.submeshCount; i++) {
				const ModelComponent::Submesh& sm = model.submeshes[i];
				if (model.indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
			}
		}
	}

} // namespace bagel
