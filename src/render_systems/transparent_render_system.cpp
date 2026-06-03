#include "transparent_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <algorithm>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

	TransparentRenderSystem::TransparentRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(GBufferPushConstantData) }
		, descriptorManager{ _descriptorManager }
		, registry{ _registry }
	{
		std::cout << "Creating Transparent Render System\n";
		createPipeline(renderPass,
			"/shaders/gbuffer_fill.vert.spv",
			"/shaders/transparent.frag.spv",
			BGLPipeline::setupTransparentPipeline);
	}

	void TransparentRenderSystem::render(FrameInfo& frameInfo)
	{
		struct DrawItem {
			entt::entity entity;
			float        distanceSq;
		};

		glm::vec3 camPos = frameInfo.camera.getPosition();
		std::vector<DrawItem> items;

		auto view = registry.view<TransformComponent, ModelComponent, TransparentComponent>();
		for (auto entity : view) {
			auto& transform = view.get<TransformComponent>(entity);
			glm::vec3 diff = transform.getWorldTranslation() - camPos;
			items.push_back({ entity, glm::dot(diff, diff) });
		}

		if (items.empty()) return;

		std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
			return a.distanceSq > b.distanceSq;
		});

		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize offsets[] = { 0 };

		for (const DrawItem& item : items) {
			auto& transform = registry.get<TransformComponent>(item.entity);
			auto& model     = registry.get<ModelComponent>(item.entity);

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);
			if (model.indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			for (uint32_t i = 0; i < model.submeshCount; i++) {
				const ModelComponent::Submesh& sm  = model.submeshes[i];
				const Material&               mat = model.materials[i];
				GBufferPushConstantData push{};
				push.UsesBufferedTransform = 0;
				push.modelMatrix = transform.mat4();
				push.scale       = glm::vec4{ transform.getWorldScale(), 1.0f };
				push.albedoMap   = mat.albedoMap;
				push.normalMap   = mat.normalMap;
				push.roughMap    = mat.roughMap;
				push.emissionMap = mat.emissionMap;

				vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0, sizeof(GBufferPushConstantData), &push);

				if (model.indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, model.vertexCount, 1, sm.firstIndex, 0);
			}
		}
	}

} // namespace bagel
