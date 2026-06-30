#include "shadow_render_system.hpp"
#include "../bagel_frustum.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <stdexcept>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

	ShadowRenderSystem::ShadowRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(ShadowPushData) }
		, descriptorManager{ _descriptorManager }
		, registry{ _registry }
	{
		createPipeline(renderPass,
			"/shaders/shadow.vert.spv",
			"/shaders/shadow.frag.spv",
			BGLPipeline::setupShadowMapPipeline);
	}

	static void sendShadowPush(VkCommandBuffer cmd, VkPipelineLayout layout, ShadowPushData& push)
	{
		vkCmdPushConstants(cmd, layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(ShadowPushData), &push);
	}

	void ShadowRenderSystem::renderShadowCasters(FrameInfo& frameInfo, uint32_t cascadeIndex, const glm::mat4& lightVP)
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

		// Cull casters that don't reach this cascade's shadow volume — otherwise every caster
		// is redrawn into all 4 cascades. The cascade frustum already bakes in casterRange, so
		// geometry behind the slice that still casts in is kept.
		Frustum cascadeFrustum;
		cascadeFrustum.extractFromVP(lightVP);

		// All submeshes cast shadows, drawn opaque into the depth map: a "transparent" submesh
		// (alpha-tested cutout — foliage, fences) still has opaque texels that must cast. This is
		// the conservative choice; per-texel alpha-tested shadows would need an alpha discard in
		// shadow.frag.
		auto singleGroup = registry.view<TransformComponent, ModelComponent>();
		for (auto [entity, transform, model] : singleGroup.each()) {
			if (model.isSkinned) continue; // skinned casters use SkinnedShadowRenderSystem (animated pose)
			glm::mat4 modelMatrix = transform.getMat4();
			if (model.frustumCull && !cascadeFrustum.testAABB(model.aabbMin, model.aabbMax, modelMatrix))
				continue;
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);
			if (model.indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			ShadowPushData push{};
			push.UsesBufferedTransform = 0;
			push.modelMatrix           = modelMatrix;
			push.cascadeIndex          = cascadeIndex;
			sendShadowPush(frameInfo.commandBuffer, pipelineLayout, push);

			for (uint32_t i = 0; i < model.submeshCount; i++) {
				const ModelComponent::Submesh& sm = model.submeshes[i];
				if (model.indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
			}
		}

		// Instanced entities
		auto instancedGroup = registry.view<TransformArrayComponent, ModelComponent>();
		for (auto [entity, transform, model] : instancedGroup.each()) {
			if (model.isSkinned) continue; // skinned models are not instanced/buffered
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);
			if (model.indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			ShadowPushData push{};
			push.UsesBufferedTransform   = transform.useBuffer() ? 1 : 0;
			push.BufferedTransformHandle = transform.bufferHandle;
			push.cascadeIndex            = cascadeIndex;
			if (!transform.useBuffer())
				push.modelMatrix = transform.mat4(0);
			sendShadowPush(frameInfo.commandBuffer, pipelineLayout, push);

			for (uint32_t i = 0; i < model.submeshCount; i++) {
				const ModelComponent::Submesh& sm = model.submeshes[i];
				if (model.indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, transform.count(), sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, transform.count(), sm.firstVertex, 0);
			}
		}
	}

} // namespace bagel
