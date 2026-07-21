#include "planet_render_system.hpp"

#include <iostream>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "ecs/components/transform.hpp"
#include "planet/components/planet.hpp"

namespace bagel {

	// Planets are shaded by an elevation colour gradient (no texture maps), evaluated per-fragment.
	// The gradient stops, the mesh's elevation span, and the base radius are all delivered here.
	struct PlanetPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 params{ 0.0f };                     // x=radius, y=minElevation, z=maxElevation, w=gradientCount
		glm::vec4 gradient[MAX_GRADIENT_POINTS];      // per stop: xyz=colour, w=height key in [0,1]
	};

	PlanetRenderSystem::PlanetRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(PlanetPushConstantData) }
		, registry{ _registry }
	{
		std::cout << "Creating Planet Render System\n";
		createPipeline(renderPass,
			"/shaders/planet_gbuffer.vert.spv",
			"/shaders/planet_gbuffer.frag.spv",
			BGLPipeline::setupGBufferPipeline);
	}

	void PlanetRenderSystem::renderEntities(FrameInfo& frameInfo)
	{
		const Frustum& frustum = frameInfo.cameraFrustum;

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
			if (model.mesh().vertexBuffer == VK_NULL_HANDLE || model.mesh().vertexCount == 0) continue;

			glm::mat4 modelMatrix = transform.getMat4();
			if (model.frustumCull && !frustum.testAABB(model.mesh().aabbMin, model.mesh().aabbMax, modelMatrix))
				continue;

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.mesh().vertexBuffer, offsets);
			if (model.mesh().indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.mesh().indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			PlanetPushConstantData push{};
			push.modelMatrix = modelMatrix;
			push.params = glm::vec4(planet.radius, planet.minElevation, planet.maxElevation,
				static_cast<float>(planet.gradientCount));
			for (int i = 0; i < planet.gradientCount && i < MAX_GRADIENT_POINTS; i++)
				push.gradient[i] = glm::vec4(planet.gradient[i].color, planet.gradient[i].height);
			vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(PlanetPushConstantData), &push);

			// Solid submeshes only — the transparent ocean submesh (if any) is drawn later by the
			// forward water/transparent pass, matching how GBufferRenderSystem handles models.
			for (const ModelComponent::Submesh& sm : model.solidSubmeshes()) {
				if (model.frustumCull && !frustum.testAABB(sm.aabbMin, sm.aabbMax, modelMatrix))
					continue;
				if (model.mesh().indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
			}
		}
	}

} // namespace bagel
