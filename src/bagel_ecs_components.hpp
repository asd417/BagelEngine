#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <iostream>
#include "bagel_engine_device.hpp"
//#include "bagel_textures.h"

// GLM functions will expect radian angles for all its functions
//#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#define BINDLESS

namespace bagel {
	struct TransformComponent {
		std::vector<glm::vec3> translation{ {0.f,0.f,0.f} };
		std::vector<glm::vec3> scale{ {-0.1f,-0.1f,-0.1f} };
		std::vector<glm::vec3> rotation{ {0.f,0.f,0.f} };
		glm::mat4 mat4(uint32_t index);
		glm::mat3 normalMatrix(uint32_t index);
		TransformComponent() {}
		TransformComponent(float x, float y, float z) { translation = { {x,y,z} }; }
		TransformComponent(glm::vec4 loc) { translation[0] = loc; }
		void addTransform(glm::vec3 _translation, glm::vec3 _scale = { -0.1f,-0.1f,-0.1f }, glm::vec3 _rotation = { 0.f,0.f,0.f })
		{
			translation.push_back(_translation);
			scale.push_back(_scale);
			rotation.push_back(_rotation);
		}
		void setTransform(uint32_t index, glm::vec3 _translation, glm::vec3 _scale = { -0.1f,-0.1f,-0.1f }, glm::vec3 _rotation = { 0.f,0.f,0.f })
		{
			if (index < translation.size()) {
				translation[index] = _translation;
				scale[index] = _scale;
				rotation[index] = _rotation;
			}
			else throw("Tried to access transform at index out of bounds");
		}
		void resetTransform() {
			translation = { {0.f,0.f,0.f} };
			scale = { {-0.1f,-0.1f,-0.1f} };
			rotation = { {0.f,0.f,0.f} };
		}
	};
	struct PointLightComponent {
		glm::vec4 color = { 1.0f,1.0f,1.0f,1.0f }; //4th is strength
		float radius = 1.0f;
	};
	struct ModelDescriptionComponent {
		std::string modelName = "";

		VkBuffer vertexBuffer = nullptr;
		VkDeviceMemory vertexMemory = nullptr;
		uint32_t vertexCount = 0;

		bool hasIndexBuffer = false;
		VkBuffer indexBuffer = nullptr;
		VkDeviceMemory indexMemory = nullptr;
		uint32_t indexCount = 0;

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
	struct TextureComponent {
		VkSampler      sampler;
		VkImage        image;
		VkImageLayout  image_layout;
		VkDeviceMemory device_memory;
		VkImageView    view;
		uint32_t       width, height;
		uint32_t       mip_levels;
		uint32_t textureHandle;

		BGLDevice& bglDevice;
		TextureComponent(BGLDevice& device);
		~TextureComponent();
		VkDescriptorImageInfo getDescriptorImageInfo() const { return { sampler , view , image_layout }; }
	};

}