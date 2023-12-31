#pragma once

#include "bgl_camera.hpp"
#include "bgl_gameobject.hpp"

#include <vulkan/vulkan.h>
#include "entt.hpp"
#define MAX_LIGHTS 10

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
		//alignas(16) glm::vec3 lightDir = glm::normalize(glm::vec3{ 1.0f,-3.0,-1.0f });
		glm::vec4 ambientLightColor{ 1.f,1.f,1.f,0.01f };
		PointLight pointLights[MAX_LIGHTS];
		int numLights;
		void updateCameraInfo(glm::mat4 projMat, glm::mat4 viewMat, glm::mat4 inverseViewMat) {
			projectionMatrix = projMat;
			viewMatrix = viewMat;
			inverseViewMatrix = inverseViewMat;
		}
	};
}