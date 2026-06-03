#include "gbuffer_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"
#include "../bagel_util.hpp"

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

	GBufferRenderSystem::GBufferRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(GBufferPushConstantData) }
		, descriptorManager{ _descriptorManager }
		, registry{ _registry }
	{
		std::cout << "Creating GBuffer Render System\n";
		createPipeline(renderPass,
			"/shaders/gbuffer_fill.vert.spv",
			"/shaders/gbuffer_fill.frag.spv",
			BGLPipeline::setupGBufferPipeline);
	}

	static void SendGBufferPush(VkCommandBuffer cmd, VkPipelineLayout layout, GBufferPushConstantData& push)
	{
		vkCmdPushConstants(cmd, layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(GBufferPushConstantData), &push);
	}

	static void FillMaterialPush(GBufferPushConstantData& push, const Material& mat)
	{
		push.albedoMap   = mat.albedoMap;
		push.normalMap   = mat.normalMap;
		push.roughMap    = mat.roughMap;
		push.emissionMap = mat.emissionMap;
	}

	void GBufferRenderSystem::renderEntities(FrameInfo& frameInfo)
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

		auto singleGroup = registry.view<TransformComponent, ModelComponent>(entt::exclude<TransparentComponent>);
		for (auto [entity, transform, model] : singleGroup.each()) {
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);
			if (model.indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			for (uint32_t i = 0; i < model.submeshCount; i++) {
				const ModelComponent::Submesh& sm = model.submeshes[i];
				GBufferPushConstantData push{};
				push.UsesBufferedTransform = 0;
				push.modelMatrix = transform.mat4();
				push.scale       = glm::vec4{ transform.getWorldScale(), 1.0f };
				FillMaterialPush(push, model.materials[i]);
				SendGBufferPush(frameInfo.commandBuffer, pipelineLayout, push);

				if (model.indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, model.vertexCount, 1, sm.firstIndex, 0);
			}
		}

		auto instancedGroup = registry.view<TransformArrayComponent, ModelComponent>(entt::exclude<TransparentComponent>);
		for (auto [entity, transform, model] : instancedGroup.each()) {
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);
			if (model.indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			for (uint32_t i = 0; i < model.submeshCount; i++) {
				const ModelComponent::Submesh& sm = model.submeshes[i];
				GBufferPushConstantData push{};
				push.UsesBufferedTransform   = transform.useBuffer() ? 1 : 0;
				push.BufferedTransformHandle = transform.bufferHandle;
				FillMaterialPush(push, model.materials[i]);
				if (!transform.useBuffer()) {
					push.modelMatrix = transform.mat4(0);
					push.scale       = glm::vec4{ transform.getWorldScale(0), 1.0f };
				}
				SendGBufferPush(frameInfo.commandBuffer, pipelineLayout, push);

				if (model.indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, transform.count(), sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, model.vertexCount, transform.count(), sm.firstIndex, 0);
			}
		}
	}

} // namespace bagel
