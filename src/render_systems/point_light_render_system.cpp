#include "point_light_render_system.hpp"

#include <iostream>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "ecs/components/light.hpp"
#include "ecs/components/transform.hpp"

namespace bagel {
	PointLightSystem::PointLightSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry,
		BGLDevice& bglDevice) :
		BGLRenderSystem{ renderPass, setLayouts, sizeof(PointLightPushConstant) },
		registry{ _registry },
		descriptorManager{ _descriptorManager }
	{
		std::cout << "Creating PointLight Render System (lighting-data only; no draw pass)\n";
	}
	// Update Position
	void PointLightSystem::update(GlobalUBO& ubo, float frameTime)
	{
		// Copy all point light information into the globalubo point light information
		int lightIndex = 0;
		auto group = registry.group<>(entt::get<TransformComponent, PointLightComponent>);
		for (auto [entity, transformComp, pointLightComp] : group.each()) {
			PointLight light{};
			light.color = glm::vec4(glm::vec3(pointLightComp.color), pointLightComp.lux);
			light.position = transformComp.getWorldTranslation();
			ubo.pointLights[lightIndex] = light;
			lightIndex++;
		}
		ubo.numLights = lightIndex;
	}

}

