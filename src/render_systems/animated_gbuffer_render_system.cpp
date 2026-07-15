#include "animated_gbuffer_render_system.hpp"

#include <iostream>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "math/bagel_math.hpp"
#include "ecs/components/model.hpp"
#include "ecs/components/transform.hpp"

namespace bagel {

	AnimatedGBufferRenderSystem::AnimatedGBufferRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(SkinnedGBufferPushConstantData) }
		, registry{ _registry }
		, descriptorManager{ _descriptorManager }
	{
		std::cout << "Creating Skinned GBuffer Render System\n";
		// Reuses the static G-buffer fragment shader; only the vertex shader skins.
		createPipeline(renderPass,
			"/shaders/skinned_gbuffer.vert.spv",
			"/shaders/gbuffer_fill.frag.spv",
			BGLPipeline::setupGBufferPipeline);
	}

	void AnimatedGBufferRenderSystem::renderEntities(FrameInfo& frameInfo)
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

		// Skinned entities only. Only the hot AnimationPlaybackComponent is needed here —
		// animBaseOffset() reads its cached scalars, so the cold AnimationComponent is never loaded.
		// Read-only over animation state: time is advanced once per frame in the engine loop
		// (before shadow + g-buffer) so both passes sample the same pose.
		auto view = registry.view<TransformComponent, ModelComponent, AnimationPlaybackComponent>();
		for (auto [entity, transform, model, anim] : view.each()) {
			if (!model.mesh().isSkinned) continue;

			glm::mat4 modelMatrix = transform.getMat4();
			// NOTE: AABB cull skipped — the model-space AABB is the bind pose, not the deformed
			// pose, so it can wrongly cull an animated mesh. A skinned-bounds pass is future work.

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.mesh().vertexBuffer, offsets);
			if (model.mesh().indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.mesh().indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			SkinnedGBufferPushConstantData push{};
			push.modelMatrix       = modelMatrix;
			push.scale             = glm::vec4{ transform.getWorldScale(), 1.0f };
			push.skinVertexBase    = model.mesh().skinVertexBase;
			push.animBaseOffset    = anim.animBaseOffset();
			push.materialRowBase   = model.mesh().skinBase + model.skinIndex * model.mesh().numSlots;
			push.fallbackAlbedoMap = frameInfo.fallbackAlbedoMap;
			vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(SkinnedGBufferPushConstantData), &push);

			// Solid submeshes only — transparent skinned submeshes are out of scope for now.
			for (const ModelComponent::Submesh& sm : model.solidSubmeshes()) {
				if (model.mesh().indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
			}
		}
	}

} // namespace bagel
