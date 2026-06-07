#pragma once

#include "bagel_camera.hpp"
#include "bagel_gameobject.hpp"

#include <vulkan/vulkan.h>
#include "entt.hpp"
#define MAX_LIGHTS 10
#define MAX_MODELS 

namespace bagel {
	struct PointLight {
		//vec4 for memory alignment
		glm::vec4 position{}; // w = bloom halo radius in pixels
		glm::vec4 color{}; // w intensity
	};

	struct FrameInfo {
		float frameTime;
		float time;          // cumulative elapsed time in seconds
		VkCommandBuffer commandBuffer;
		BGLCamera& camera;
		VkDescriptorSet globalDescriptorSets;
		entt::registry& registry;
		uint32_t fallbackAlbedoMap = 0;
	};
	// UBO struct for pre-composition stage of deferred rendering. Feed in color, position, etc
	struct GlobalUBO {
		glm::mat4 projectionMatrix{ 1.f };
		glm::mat4 viewMatrix{ 1.f };
		glm::mat4 inverseViewMatrix{ 1.f };
		//To be moved to deferred rendering ubo
		glm::vec4 ambientLightColor{ 1.f,1.f,1.f,0.01f };

		PointLight pointLights[MAX_LIGHTS];
		uint32_t numLights;
		uint32_t _pad[3]; // std140: align next vec4 to 16-byte boundary after uint
		//Wireframe setting
		glm::vec4 lineColor{ 1.f,1.f,1.f,1.f };

		glm::mat4 invViewProjMatrix{ 1.f };
		float exposure = 0.0025f;

		void updateCameraInfo(glm::mat4 projMat, glm::mat4 viewMat, glm::mat4 inverseViewMat, glm::mat4 invViewProjMat) {
			projectionMatrix   = projMat;
			viewMatrix         = viewMat;
			inverseViewMatrix  = inverseViewMat;
			invViewProjMatrix  = invViewProjMat;
		}
	};
	// UBO struct for composition stage of deferred rendering. Feed in light info
	struct CompositionUBO {
		glm::vec4 ambientLightColor{ 1.f,1.f,1.f,0.01f };
		PointLight pointLights[MAX_LIGHTS];
		uint32_t numLights;
	};

}