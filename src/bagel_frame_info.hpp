#pragma once

#include "bgl_camera.hpp"
#include "bgl_gameobject.hpp"

#include <vulkan/vulkan.h>
#include "entt.hpp"
#define MAX_LIGHTS 10
#define MAX_MODELS 

namespace bagel {
	struct PointLight {
		//vec4 for memory alignment
		glm::vec4 position{}; // ignore w
		glm::vec4 color{}; // w intensity
	};

	struct FrameInfo {
		float frameTime;
		VkCommandBuffer commandBuffer;
		BGLCamera& camera;
		VkDescriptorSet globalDescriptorSets;
		entt::registry& registry;
	};

	struct GlobalUBO {
		glm::mat4 projectionMatrix{ 1.f };
		glm::mat4 viewMatrix{ 1.f };
		glm::mat4 inverseViewMatrix{ 1.f };

		glm::vec4 ambientLightColor{ 1.f,1.f,1.f,0.01f };

		PointLight pointLights[MAX_LIGHTS];
		uint32_t numLights;

		//Wireframe setting
		glm::vec4 lineColor{ 1.f,1.f,1.f,1.f };

		void updateCameraInfo(glm::mat4 projMat, glm::mat4 viewMat, glm::mat4 inverseViewMat) {
			projectionMatrix = projMat;
			viewMatrix = viewMat;
			inverseViewMatrix = inverseViewMat;
		}
	};

}