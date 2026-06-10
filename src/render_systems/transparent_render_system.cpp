#include "transparent_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"
#include "../bagel_util.hpp"

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <utility>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

	TransparentRenderSystem::TransparentRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(TransparentPushConstantData) }
		, descriptorManager{ _descriptorManager }
		, registry{ _registry }
	{
		std::cout << "Creating Transparent Render System\n";
		createPipeline(renderPass,
			"/shaders/transparent.vert.spv",
			"/shaders/transparent.frag.spv",
			BGLPipeline::setupTransparentPipeline);
	}

	static void SendTransparentPush(VkCommandBuffer cmd, VkPipelineLayout layout, TransparentPushConstantData& push)
	{
		vkCmdPushConstants(cmd, layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(TransparentPushConstantData), &push);
	}

	void TransparentRenderSystem::renderEntities(FrameInfo& frameInfo)
	{
		// Gather transparent entities and sort back-to-front by camera distance so the
		// alpha blend composites correctly. Sorting a local list avoids mutating entt's
		// component storage order (which other systems iterate by).
		glm::vec3 camPos = frameInfo.camera.getPosition();
		auto view = registry.view<TransformComponent, ModelComponent>();

		//sort by distance
		std::vector<std::pair<float, entt::entity>> order;
		for (auto [entity, transform, model] : view.each()) {
			if (!model.hasTransparent()) continue;
			glm::vec3 d = camPos - transform.getTranslation();
			order.emplace_back(glm::dot(d, d), entity);
		}
		if (order.empty()) return;
		// Far first (descending squared distance)
		std::sort(order.begin(), order.end(),
			[](const auto& a, const auto& b) { return a.first > b.first; });

		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize offsets[] = { 0 };

		for (const auto& [dist, entity] : order) {
			auto& transform = view.get<TransformComponent>(entity);
			auto& model     = view.get<ModelComponent>(entity);

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);
			if (model.indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			TransparentPushConstantData push{};
			push.UsesBufferedTransform = 0;
			push.modelMatrix = transform.mat4();
			push.scale       = glm::vec4{ transform.getWorldScale(), 1.0f };
			SendTransparentPush(frameInfo.commandBuffer, pipelineLayout, push);

			for (const ModelComponent::Submesh& sm : model.transparentSubmeshes()) {
				if (model.indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
			}
		}
	}

} // namespace bagel
