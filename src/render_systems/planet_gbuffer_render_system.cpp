#include "planet_gbuffer_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../components/planet.hpp"
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <algorithm>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

	PlanetGBufferRenderSystem::PlanetGBufferRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(PlanetGBufferPushConstantData) }
		, registry{ _registry }
	{
		std::cout << "Creating Planet GBuffer Render System\n";
		createPipeline(renderPass,
			"/shaders/planet_gbuffer.vert.spv",
			"/shaders/planet_gbuffer.frag.spv",
			BGLPipeline::setupGBufferPipeline);
	}

	void PlanetGBufferRenderSystem::renderEntities(FrameInfo& frameInfo)
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

		auto group = registry.view<TransformComponent, ModelComponent, PlanetComponent>();
		for (auto [entity, transform, model, planet] : group.each()) {
			if (model.vertexBuffer == VK_NULL_HANDLE || model.vertexCount == 0) continue;

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.vertexBuffer, offsets);

			PlanetGBufferPushConstantData push{};
			push.modelMatrix  = transform.getMat4();
			if (false) {

			} else {
				push.centerRadius = glm::vec4(transform.getWorldTranslation(), 1.0f);
			}
			vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(push), &push);

			// Planet mesh is a non-indexed triangle soup (see PlanetComponentSystem). Draw ONLY
			// the solid submesh(es) = the terrain; the transparent ocean submesh is drawn later
			// by TransparentRenderSystem with the water shader.
			for (const ModelComponent::Submesh& sm : model.solidSubmeshes())
				vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
		}
	}

} // namespace bagel
