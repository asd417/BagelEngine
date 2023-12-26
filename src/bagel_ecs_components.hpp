#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <exception>
#include <memory>

#include "bagel_engine_device.hpp"
#include "bagel_descriptors.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

// GLM functions will expect radian angles for all its functions
//#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#define MAX_TRANSFORM_PER_ENT 1000
#define COLLIDER_PER_ENT 32

namespace bagel {
	struct DataBufferComponent {
		std::unique_ptr<BGLBuffer> objDataBuffer;
		uint32_t bufferHandle;

		DataBufferComponent(BGLDevice& device, BGLBindlessDescriptorManager& descriptorManager, uint32_t bufferUnitsize);
		~DataBufferComponent();
		void writeToBuffer(void* data, size_t size, size_t offset);
		uint32_t getBufferHandle() const { return bufferHandle; }
	};

	struct TransformComponent {
		TransformComponent() = default;
		TransformComponent(float x, float y, float z) { translation = { x,y,z }; }
		TransformComponent(glm::vec4 pos) { translation = glm::vec3(pos); }
		glm::mat4	mat4();
		glm::mat3	normalMatrix();

		glm::vec3	getTranslation() const;
		void		setTranslation(glm::vec3 _translation);
		glm::vec3	getScale() const { return scale; }
		void		setScale(glm::vec3 _scale);
		glm::vec3	getRotation() const { return rotation; }
		void		setRotation(glm::vec3 _rotation);

		glm::vec3	getLocalTranslation() const;
		void		setLocalTranslation(glm::vec3 _translation);
		glm::vec3	getLocalScale() const { return localScale; }
		void		setLocalScale(glm::vec3 _scale);
		glm::vec3	getLocalRotation() const { return localRotation; }
		void		setLocalRotation(glm::vec3 _rotation);

		//Internally, y cooridnate is flipped.
	private:
		glm::vec3 translation = { 0.0f,0.0f,0.0f };
		glm::vec3 scale = { 0.1f, 0.1f, 0.1f };
		glm::vec3 rotation = { 0.f,0.f,0.f };

		glm::vec3 localTranslation = { 0.0f,0.0f,0.0f };
		glm::vec3 localScale = { 1.0f, 1.0f, 1.0f };
		glm::vec3 localRotation = { 0.f,0.f,0.f };
	};

	struct TransformArrayComponent {
		struct TransformBufferUnit {
			glm::mat4 modelMatrix{ 1.0f };
			glm::mat4 normalMatrix{ 1.0f };
		};
		//TransformComponent will by default hold 1 transform value
		uint32_t maxIndex = 1;
		glm::mat4 mat4(uint32_t index = 0);
		glm::mat3 normalMatrix(uint32_t index = 0);

		// If using buffer, bufferHandle will store the buffer handle index
		bool usingBuffer = false;
		uint32_t bufferHandle = 0;

		TransformArrayComponent() { resetTransform(); }
		TransformArrayComponent(float x, float y, float z) { resetTransform(); translation[0] = { x,y,z };}
		TransformArrayComponent(glm::vec4 loc) { resetTransform(); translation[0] = glm::vec3(loc);}
		bool useBuffer() const { return usingBuffer; }

		void addTransform(glm::vec3 _translation, glm::vec3 _scale = { -0.1f,-0.1f,-0.1f }, glm::vec3 _rotation = { 0.f,0.f,0.f })
		{
			if (maxIndex < MAX_TRANSFORM_PER_ENT) {
				_translation.y *= -1;
				translation[maxIndex] = _translation;
				scale[maxIndex] = _scale;
				rotation[maxIndex] = _rotation;
				maxIndex++;
			}
			else std::cout << "Transform Array Full. MAX_TRANSFORM_PER_ENT: " << MAX_TRANSFORM_PER_ENT << "\n";
		}
		void setTransform(uint32_t index, glm::vec3 _translation, glm::vec3 _scale = { -0.1f,-0.1f,-0.1f }, glm::vec3 _rotation = { 0.f,0.f,0.f })
		{
			if (index < MAX_TRANSFORM_PER_ENT) {
				_translation.y *= -1;
				translation[index] = _translation;
				scale[index] = _scale;
				rotation[index] = _rotation;
			}
			else throw("index out of bounds: TransformComponent::translation   MAX_TRANSFORM_PER_ENT: " + MAX_TRANSFORM_PER_ENT);
		}
		void resetTransform() {
			translation.fill({ 0.f,0.f,0.f });
			scale.fill({ -0.1f,-0.1f,-0.1f });
			rotation.fill({ 0.f,0.f,0.f });
			maxIndex = 1;
		}
		void removeLastNTransform(uint32_t n = 1) {
			maxIndex = n > maxIndex ? 0 : maxIndex - n;
		}
		void ToBufferComponent(DataBufferComponent& bufferComponent);

	private:
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> translation;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> scale;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> rotation;
	};
	struct PointLightComponent {
		glm::vec4 color = { 1.0f,1.0f,1.0f,1.0f }; //4th is strength
		float radius = 1.0f;
	};
	struct ModelDescriptionComponent {
		enum TextureCompositeFlag {
			DIFFUSE =	0b0000'0001,
			EMISSION =	0b0000'0010,
			NORMAL =	0b0000'0100,
			ROUGHMETAL= 0b0000'1000
		};
		std::string modelName = "";

		VkBuffer vertexBuffer = nullptr;
		VkDeviceMemory vertexMemory = nullptr;
		uint32_t vertexCount = 0;

		bool hasIndexBuffer = false;
		VkBuffer indexBuffer = nullptr;
		VkDeviceMemory indexMemory = nullptr;
		uint32_t indexCount = 0;

		uint8_t textureMapFlag;
		
		ModelDescriptionComponent() = default;
		~ModelDescriptionComponent() {
			std::cout << "Destroying Model Description Component" << "\n";
			if(vertexBuffer != nullptr) vkDestroyBuffer(BGLDevice::device(), vertexBuffer, nullptr);
			if (vertexMemory != nullptr) vkFreeMemory(BGLDevice::device(), vertexMemory, nullptr);
			if (hasIndexBuffer) {
				if (indexBuffer != nullptr) vkDestroyBuffer(BGLDevice::device(), indexBuffer, nullptr);
				if (indexMemory != nullptr) vkFreeMemory(BGLDevice::device(), indexMemory, nullptr);
			}
		};
	};
	struct GeneratedWireframeComponent {
		VkBuffer vertexBuffer = nullptr;
		VkDeviceMemory vertexMemory = nullptr;
		uint32_t vertexCount = 0;

		bool hasIndexBuffer = false;
		VkBuffer indexBuffer = nullptr;
		VkDeviceMemory indexMemory = nullptr;
		uint32_t indexCount = 0;

		bool mapped = false;

		GeneratedWireframeComponent() = default;
		~GeneratedWireframeComponent() {
			std::cout << "Destroying Model Description Component" << "\n";
			if (vertexBuffer != nullptr) vkDestroyBuffer(BGLDevice::device(), vertexBuffer, nullptr);
			if (vertexMemory != nullptr) vkFreeMemory(BGLDevice::device(), vertexMemory, nullptr);
			if (hasIndexBuffer) {
				if (indexBuffer != nullptr) vkDestroyBuffer(BGLDevice::device(), indexBuffer, nullptr);
				if (indexMemory != nullptr) vkFreeMemory(BGLDevice::device(), indexMemory, nullptr);
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
		bool duplicate = false;

		TextureComponent() = default;
		~TextureComponent();
		VkDescriptorImageInfo getDescriptorImageInfo() const { return { sampler , view , image_layout }; }
	};

	struct DiffuseTextureComponent : TextureComponent {};
	struct EmissionTextureComponent : TextureComponent {};
	struct NormalTextureComponent : TextureComponent {};
	struct RoughnessMetalTextureComponent : TextureComponent {};

#define MAXPHYSICSBODYCOUNT 64
	//All physics related calculations are done inside jolt system. This component is a container for the pointeres to the bodies.
	struct JoltPhysicsComponent {
		JPH::BodyID bodyID;
		JPH::BodyID bodyIDArray[MAXPHYSICSBODYCOUNT];
		uint8_t bodyCount = 0;
		JoltPhysicsComponent() = default;
		~JoltPhysicsComponent() = default;

		void AddBody(JPH::BodyID id) {
			if (bodyCount == MAXPHYSICSBODYCOUNT) {
				std::cout << "Physics body count max\n";
				return;
			}
			bodyIDArray[bodyCount] = id;
			bodyCount++;
		}
		void RemoveBody(JPH::BodyID id) {
			uint8_t foundI = MAXPHYSICSBODYCOUNT;
			for (uint8_t i = 0; i < bodyCount;i++) {
				if (bodyIDArray[i] == id) {
					foundI = i;
				};
				if (foundI < MAXPHYSICSBODYCOUNT) {
					if(i < MAXPHYSICSBODYCOUNT - 1) bodyIDArray[i] = bodyIDArray[i + 1];
					else bodyIDArray[i] = JPH::BodyID{};
				}
			}
			bodyCount--;
		}
	};
	
#ifdef PHYSICSTEST
	struct PhysicsComponent {
		glm::vec3 velocity;
		glm::vec3 angularVelocity;
		glm::vec3 COM;
		float mass = 1.0f;
		float bounciness = 0.8f;
		bool gravity = true;
		uint8_t colliderTypeFlag;
		glm::vec3 frameVelocity = { 0.0f,0.0f,0.0f };
	};

	struct SphereColliderComponent {
		uint8_t colliderCount = 0;
		// Center of sphere in object space
		std::array<glm::vec3, COLLIDER_PER_ENT> center;
		std::array<float, COLLIDER_PER_ENT> radius;
		void AddCollider(glm::vec3 c, float r) {
			center[colliderCount] = c;
			radius[colliderCount] = r;
			colliderCount++;
		}
	};

	struct PlaneColliderComponent {
		uint8_t colliderCount = 0;
		std::array<glm::vec3, COLLIDER_PER_ENT> normal;
		std::array<float, COLLIDER_PER_ENT> distance;
	};

	struct CapsuleColliderComponent {
		uint8_t colliderCount = 0;
		std::array<glm::vec3, COLLIDER_PER_ENT> center1;
		std::array<glm::vec3, COLLIDER_PER_ENT> center2;
		std::array<glm::vec3, COLLIDER_PER_ENT> c1c2;
		std::array<float, COLLIDER_PER_ENT> radius;
	};
#endif
	
}