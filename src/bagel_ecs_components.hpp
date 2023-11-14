#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <iostream>
#include "bagel_engine_device.hpp"

// GLM functions will expect radian angles for all its functions
//#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {
	struct TransformComponent {
		std::vector<glm::vec3> translation{ {0.f,0.f,0.f} };
		std::vector<glm::vec3> scale{ {1.f,1.f,1.f} };
		std::vector<glm::vec3> rotation{ {0.f,0.f,0.f} };
		glm::mat4 mat4(uint32_t index);
		glm::mat3 normalMatrix(uint32_t index);
	};
	struct PointLightComponent {
		float lightIntensity = 1.0f;
	};
	struct ModelDescriptionComponent {
		VkBuffer vertexBuffer = nullptr;
		VkDeviceMemory vertexMemory = nullptr;
		uint32_t vertexCount = 0;
		bool hasIndexBuffer = false;
		VkBuffer indexBuffer = nullptr;
		VkDeviceMemory indexMemory = nullptr;
		uint32_t indexCount = 0;
		VkDescriptorSet descriptorSet = nullptr;
		VkDeviceMemory memory = nullptr;
		bool mapped = false;
		BGLDevice& bglDevice;
		ModelDescriptionComponent(BGLDevice& device) : bglDevice{ device } {};
		~ModelDescriptionComponent() {
			std::cout << "Destroying Model Description Component" << "\n";
			if(vertexBuffer != nullptr) vkDestroyBuffer(bglDevice.device(), vertexBuffer, nullptr);
			if (vertexMemory != nullptr) vkFreeMemory(bglDevice.device(), vertexMemory, nullptr);
			if (hasIndexBuffer) {
				if (indexBuffer != nullptr) vkDestroyBuffer(bglDevice.device(), indexBuffer, nullptr);
				if (indexMemory != nullptr) vkFreeMemory(bglDevice.device(), indexMemory, nullptr);
			}
		};
	};
}