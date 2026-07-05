#include "water_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../components/planet.hpp"   // PlanetComponent
#include "../bagel_engine_device.hpp"

#include <vulkan/vulkan.h>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

	WaterRenderSystem::WaterRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		entt::registry& _registry)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(WaterPushConstantData) }
		, registry{ _registry }
	{
		std::cout << "Creating Water Render System\n";
		// Same pipeline state as the transparent pass (alpha blend into radiosity, depth-test
		// read-only, no depth write) — the water just uses its own shaders.
		createPipeline(renderPass,
			"/shaders/water.vert.spv",
			"/shaders/water.frag.spv",
			BGLPipeline::setupTransparentPipeline);
	}

	void WaterRenderSystem::renderEntities(FrameInfo& frameInfo, uint32_t gDepthHandle, float opaqueDepth, float camRefDist)
	{
		// Planet oceans only. A planet's ocean is its transparent submesh (materialIndex 1, the
		// OCEAN sentinel); the solid terrain submesh is drawn by PlanetGBufferRenderSystem. The
		// TransparentRenderSystem skips PlanetComponent entities so the ocean is drawn here instead.
		auto view = registry.view<TransformComponent, ModelComponent, PlanetComponent>();

		bool bound = false;
		VkDeviceSize offsets[] = { 0 };
		for (auto [entity, transform, model, planet] : view.each()) {
			if (model.mesh().vertexBuffer == VK_NULL_HANDLE || model.mesh().vertexCount == 0) continue;
			if (!model.hasTransparent()) continue; // mesh not rebuilt yet (no ocean submesh)

			// Bind pipeline + global descriptor set lazily, only once a planet actually draws.
			if (!bound) {
				bglPipeline->bind(frameInfo.commandBuffer);
				vkCmdBindDescriptorSets(
					frameInfo.commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					pipelineLayout,
					0, 1,
					&frameInfo.globalDescriptorSets,
					0, nullptr);
				bound = true;
			}

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.mesh().vertexBuffer, offsets);
			if (model.mesh().indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.mesh().indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			WaterPushConstantData push{};
			push.modelMatrix = transform.getMat4();
			push.scale       = glm::vec4{ transform.getWorldScale(), 1.0f };
			push.time        = frameInfo.time;
			push.gDepthHandle = gDepthHandle;
			push.opaqueDepth = opaqueDepth;
			push.camRefDist  = camRefDist;
			vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(WaterPushConstantData), &push);

			// Planet mesh is a non-indexed triangle soup; transparentSubmeshes() = the ocean.
			for (const ModelComponent::Submesh& sm : model.transparentSubmeshes()) {
				if (model.mesh().indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
			}
		}
	}

} // namespace bagel
