#pragma once

#include "bagel_camera.hpp"
#include "bagel_gameobject.hpp"

#include <vulkan/vulkan.h>
#include <cstddef>
#include "entt.hpp"
#include "engine/bagel_engine_config.hpp"
#define MAX_MODELS

namespace bagel {
	struct PointLight {
		glm::vec3 position{};
		// Max influence distance of this light (world units); also fills the std140 slot that
		// keeps `color` at offset 16 (a vec3 occupies a 16-byte slot, GLM types are alignof 4
		// here — see GlobalUBO). Sits at offset 12, i.e. position.w in shaders that declare the
		// PointLight position as a vec4.
		float     maxDistance{};
		glm::vec4 color{}; // w intensity
	};

	constexpr uint8_t SHADOW_CASCADE_COUNT = 4;

	// std140-compatible directional light data block (offset 640 inside GlobalUBO)
	struct DirectionalLightData {
		glm::vec4 direction{};           // xyz = world-space forward direction of the light
		glm::vec4 color{};               // xyz = color, w = intensity
		glm::mat4 lightSpaceMatrix[SHADOW_CASCADE_COUNT]{ glm::mat4{1.f}, glm::mat4{1.f}, glm::mat4{1.f}, glm::mat4{1.f} }; // per-cascade light view-projection
		glm::vec4 cascadeSplits{};       // view-space distance where each cascade ends
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
		uint32_t _pad1[3]{}; // std140: DirectionalLightData (struct) aligns to 16 -> offset 640. glm types have alignof 4, so padding must be explicit
		DirectionalLightData directionalLight{};  // offset 640, size 304
		uint32_t hasDirLight   = 0;               // offset 944
		uint32_t shadowMapHandle = 0;             // offset 948
		float shadowBiasMin   = 0.002f;           // offset 952
		float shadowBiasSlope = 0.005f;           // offset 956; struct ends exactly at the 960-byte std140 block size

		void updateCameraInfo(glm::mat4 projMat, glm::mat4 viewMat, glm::mat4 inverseViewMat, glm::mat4 invViewProjMat, float exp) {
			projectionMatrix   = projMat;
			viewMatrix         = viewMat;
			inverseViewMatrix  = inverseViewMat;
			invViewProjMatrix  = invViewProjMat;
			exposure = exp;
		}
	};
	// GlobalUBO is uploaded as a raw memcpy; these guard the std140 offsets the shaders declare
	static_assert(offsetof(GlobalUBO, numLights)        == 528, "GlobalUBO does not match std140 layout");
	static_assert(offsetof(GlobalUBO, lineColor)        == 544, "GlobalUBO does not match std140 layout");
	static_assert(offsetof(GlobalUBO, directionalLight) == 640, "GlobalUBO does not match std140 layout");
	static_assert(offsetof(GlobalUBO, hasDirLight)      == 944, "GlobalUBO does not match std140 layout");
	static_assert(sizeof(GlobalUBO)                     == 960, "GlobalUBO does not match std140 block size");
	// UBO struct for composition stage of deferred rendering. Feed in light info
	struct CompositionUBO {
		glm::vec4 ambientLightColor{ 1.f,1.f,1.f,0.01f };
		PointLight pointLights[MAX_LIGHTS];
		uint32_t numLights;
	};

}