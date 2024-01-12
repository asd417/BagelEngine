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

		//Light Info
		glm::vec4 ambientLightColor{ 1.f,1.f,1.f,0.01f };
		PointLight pointLights[MAX_LIGHTS];
		uint32_t numLights;

		void updateCameraInfo(glm::mat4 projMat, glm::mat4 viewMat, glm::mat4 inverseViewMat) {
			projectionMatrix = projMat;
			viewMatrix = viewMat;
			inverseViewMatrix = inverseViewMat;
		}
	};

	struct ModelUBO {
		//Model Material Info
		uint32_t diffuseTextureHandle;
		uint32_t emissionTextureHandle;
		uint32_t normalTextureHandle;
		uint32_t roughmetalTextureHandle;
		// Stores flags to determine which textures are present
		uint32_t textureMapFlag;

		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform = 0;
	};
}