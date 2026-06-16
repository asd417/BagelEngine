#include "skinned_gbuffer_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"
#include "../bagel_util.hpp"
#include "../bagel_frustum.hpp"

#include <vulkan/vulkan.h>
#include <iostream>
#include <cmath>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

	SkinnedGBufferRenderSystem::SkinnedGBufferRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(SkinnedGBufferPushConstantData) }
		, descriptorManager{ _descriptorManager }
		, registry{ _registry }
	{
		std::cout << "Creating Skinned GBuffer Render System\n";
		// Reuses the static G-buffer fragment shader; only the vertex shader skins.
		createPipeline(renderPass,
			"/shaders/skinned_gbuffer.vert.spv",
			"/shaders/gbuffer_fill.frag.spv",
			BGLPipeline::setupGBufferPipeline);
	}

	static void SendSkinnedPush(VkCommandBuffer cmd, VkPipelineLayout layout, SkinnedGBufferPushConstantData& push)
	{
		vkCmdPushConstants(cmd, layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(SkinnedGBufferPushConstantData), &push);
	}

	void SkinnedGBufferRenderSystem::renderEntities(FrameInfo& frameInfo)
	{
		Frustum frustum;
		frustum.extractFromVP(frameInfo.camera.getProjection() * frameInfo.camera.getView());

		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize offsets[] = { 0 };

		// Skinned entities only (must carry an AnimationComponent).
		// Read-only over animation state: time is advanced once per frame in the engine loop
		// (before shadow + g-buffer) so both passes sample the same pose.
		auto view = registry.view<TransformComponent, ModelComponent, AnimationComponent>();
		for (auto [entity, transform, model, anim] : view.each()) {
			if (!model.isSkinned) continue;

			glm::mat4 modelMatrix = transform.mat4();
			// NOTE: AABB cull skipped — the model-space AABB is the bind pose, not the deformed
			// pose, so it can wrongly cull an animated mesh. A skinned-bounds pass is future work.

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);
			if (model.indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			SkinnedGBufferPushConstantData push{};
			push.modelMatrix       = modelMatrix;
			push.scale             = glm::vec4{ transform.getWorldScale(), 1.0f };
			push.skinVertexBase    = model.skinVertexBase;
			push.animBaseOffset    = anim.animBaseOffset();
			push.materialRowBase   = model.skinBase + model.skinIndex * model.numSlots;
			push.fallbackAlbedoMap = frameInfo.fallbackAlbedoMap;
			SendSkinnedPush(frameInfo.commandBuffer, pipelineLayout, push);

			// Solid submeshes only — transparent skinned submeshes are out of scope for now.
			for (const ModelComponent::Submesh& sm : model.solidSubmeshes()) {
				if (model.indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
			}
		}
	}

} // namespace bagel
