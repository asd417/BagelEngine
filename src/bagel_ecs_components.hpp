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


#include "entt.hpp"
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

		DataBufferComponent(BGLDevice& device, BGLBindlessDescriptorManager& descriptorManager, uint32_t bufferUnitsize, const char* bufferName);
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
		glm::vec3	getTranslation() const { return translation; }
		void		setTranslation(const glm::vec3& _translation) { translation = _translation; }
		glm::vec3	getScale() const { return scale; }
		void		setScale(const glm::vec3& _scale) { scale = _scale; }
		glm::vec3	getRotation() const { return rotation; }
		void		setRotation(const glm::vec3& _rotation) { rotation = _rotation; }
		glm::vec3	getLocalTranslation() const { return localTranslation; }
		void		setLocalTranslation(const glm::vec3& _translation) { localTranslation = _translation; }
		glm::vec3	getLocalScale() const { return localScale; }
		void		setLocalScale(const glm::vec3& _scale){ localScale = _scale; }
		glm::vec3	getLocalRotation() const { return localRotation; }
		void		setLocalRotation(const glm::vec3& _rotation) { localRotation = _rotation; }

		glm::vec3	getWorldTranslation() const { return translation + localTranslation; };
		glm::vec3	getWorldScale() const { return { scale.x * localScale.x, scale.y * localScale.y, scale.z * localScale.z}; };
		glm::vec3	getWorldRotation() const { return rotation + localRotation; };

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
			glm::vec4 scale{ 1.0f };
		};
		//TransformComponent will by default hold 1 transform value

		//Used to track the number of elements present
		//Also where new data will be placed, overriding existing data
		uint32_t maxIndex = 1;
		glm::mat4 mat4(uint32_t index = 0);
		glm::mat3 normalMatrix(uint32_t index = 0);

		// If using buffer, bufferHandle will store the buffer handle index
		bool usingBuffer = false;
		uint32_t bufferHandle = 0;

		TransformArrayComponent() { resetTransform(); }
		TransformArrayComponent(float x, float y, float z) { resetTransform(); translation[0] = { x,y,z };}
		TransformArrayComponent(glm::vec4& loc) { resetTransform(); translation[0] = glm::vec3(loc);}
		bool useBuffer() const { return usingBuffer; }

		void addTransform(glm::vec3 _translation, glm::vec3 _scale = { -0.1f,-0.1f,-0.1f }, glm::vec3 _rotation = { 0.f,0.f,0.f });
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
			scale.fill({ 0.1f, 0.1f, 0.1f });
			rotation.fill({ 0.f,0.f,0.f });
			localTranslation.fill({ 0.f,0.f,0.f });
			localScale.fill({ 1.0f,1.0f,1.0f });
			localRotation.fill({ 0.f,0.f,0.f });
			maxIndex = 1;
		}
		void removeLastNTransform(uint32_t n = 1) {
			maxIndex = n > maxIndex ? 0 : maxIndex - n;
		}
		void ToBufferComponent(DataBufferComponent& bufferComponent);
		uint32_t count() const { return maxIndex; }

		glm::vec3	getTranslation(uint32_t i) const { return translation[i]; };
		void		setTranslation(uint32_t i, const glm::vec3& _translation) { translation[i] = _translation; };
		glm::vec3	getScale(uint32_t i) const { return scale[i]; }
		void		setScale(uint32_t i, const glm::vec3& _scale) { scale[i] = _scale; };
		glm::vec3	getRotation(uint32_t i) const { return rotation[i]; }
		void		setRotation(uint32_t i, const glm::vec3& _rotation) { rotation[i] = _rotation; };

		glm::vec3	getLocalTranslation(uint32_t i) const { return localTranslation[i]; };
		void		setLocalTranslation(uint32_t i, const glm::vec3& _translation) { localTranslation[i] = _translation; };
		glm::vec3	getLocalScale(uint32_t i) const { return localScale[i]; }
		void		setLocalScale(uint32_t i, const glm::vec3& _scale) { localScale[i] = _scale; };
		glm::vec3	getLocalRotation(uint32_t i) const { return localRotation[i]; }
		void		setLocalRotation(uint32_t i, const glm::vec3& _rotation) { localRotation[i] = _rotation; };

		glm::vec3	getWorldTranslation(uint32_t i) const { return translation[i] + localTranslation[i]; };
		glm::vec3	getWorldScale(uint32_t i) const { return { scale[i].x * localScale[i].x, scale[i].y * localScale[i].y, scale[i].z * localScale[i].z}; };
		glm::vec3	getWorldRotation(uint32_t i) const { return rotation[i] + localRotation[i]; };

	private:
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> translation;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> scale;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> rotation;

		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> localTranslation;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> localScale;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> localRotation;
	};

	struct TransformHierachyComponent {
		entt::entity parent;
		bool hasParent = false;
		uint32_t depth = 0;
	};

	struct PointLightComponent {
		glm::vec4 color = { 1.0f,1.0f,1.0f,1.0f }; //4th is strength
		float radius = 1.0f;
	};


	//Do you realistically need more than 5 textures per model?
	struct TextureComponent {
		/// <summary>
		/// Since descriptors are handled by descriptor manager, rendering only requires texture handle.
		/// This allows component to be as compact as possible
		/// </summary>
		static constexpr uint32_t MAX_TEXTURE_COUNT = 5;
		std::string textureName[MAX_TEXTURE_COUNT] = { "" };
		uint32_t    width[MAX_TEXTURE_COUNT] = { 0 };
		uint32_t	height[MAX_TEXTURE_COUNT] = { 0 };
		uint32_t    mip_levels[MAX_TEXTURE_COUNT] = { 0 };
		uint32_t	textureHandle[MAX_TEXTURE_COUNT] = { 0 };
		uint32_t	textureCount = 0;
	};

	struct DiffuseTextureComponent : TextureComponent {};
	struct EmissionTextureComponent : TextureComponent {};
	struct NormalTextureComponent : TextureComponent {};
	struct RoughnessMetalTextureComponent : TextureComponent {};

	struct ModelComponent {
		//Describes what composes the material of this submes
		enum TextureCompositeFlag {
			DIFFUSE = 1,
			EMISSION = 2,
			NORMAL = 4,
			ROUGHMETAL = 8
		};
		struct Submesh {
			//primitives in gltf terms
			uint32_t firstIndex;
			uint32_t indexCount;
			uint32_t materialIndex;

			uint32_t diffuseTextureHandle;
			uint32_t emissionTextureHandle;
			uint32_t normalTextureHandle;
			uint32_t roughmetalTextureHandle;
			// Stores flags to determine which textures are present
			uint32_t textureMapFlag;

			glm::vec4 roughmetalmultiplier{ 1.0f };
		};
		std::string modelName = "";
		std::vector<Submesh> submeshes{};

		VkBuffer vertexBuffer;
		VkBuffer indexBuffer;
		VkDeviceMemory vertexMemory;
		VkDeviceMemory indexMemory;

		uint32_t indexCount;
		uint32_t vertexCount;

		ModelComponent* origin = nullptr;
		~ModelComponent() {
			if (origin == this || origin == nullptr) {
				vkDestroyBuffer(BGLDevice::device(), vertexBuffer, nullptr);
				vkDestroyBuffer(BGLDevice::device(), indexBuffer, nullptr);
				vkFreeMemory(BGLDevice::device(), vertexMemory, nullptr);
				vkFreeMemory(BGLDevice::device(), indexMemory, nullptr);
			}
		}
		void setDiffuseTextureToSubmesh(uint32_t i, uint32_t handle) {
			assert(i < submeshes.size() && "Submesh at index does not exist");
			submeshes[i].diffuseTextureHandle = handle;
			submeshes[i].textureMapFlag |= TextureCompositeFlag::DIFFUSE;
		}
		void setEmissionTextureToSubmesh(uint32_t i, uint32_t handle) {
			assert(i < submeshes.size() && "Submesh at index does not exist");
			submeshes[i].emissionTextureHandle = handle;
			submeshes[i].textureMapFlag |= TextureCompositeFlag::EMISSION;
		}
		void setNormalTextureToSubmesh(uint32_t i, uint32_t handle) {
			assert(i < submeshes.size() && "Submesh at index does not exist");
			submeshes[i].normalTextureHandle = handle;
			submeshes[i].textureMapFlag |= TextureCompositeFlag::NORMAL;
		}
		void setRoughMetalTextureToSubmesh(uint32_t i, uint32_t handle) {
			assert(i < submeshes.size() && "Submesh at index does not exist");
			submeshes[i].roughmetalTextureHandle = handle;
			submeshes[i].textureMapFlag |= TextureCompositeFlag::ROUGHMETAL;
		}
		void setRoughMetalMultiplier(uint32_t i, glm::vec4 mult) {
			assert(i < submeshes.size() && "Submesh at index does not exist");
			submeshes[i].roughmetalmultiplier = mult;
		}
		void useDiffuseComponent(DiffuseTextureComponent& dfc);
		void useEmissionComponent(EmissionTextureComponent& dfc);
		void useNormalComponent(NormalTextureComponent& dfc);
		void useRoughMetalComponent(RoughnessMetalTextureComponent& dfc);
	};

	struct TransparentModelComponent {
		struct Submesh {
			//primitives in gltf terms
			VkBuffer vertexBuffer;
			VkBuffer indexBuffer;

			VkDeviceMemory vertexMemory;
			VkDeviceMemory indexMemory;

			uint32_t diffuseTextureHandle;
			uint32_t emissionTextureHandle;
			uint32_t normalTextureHandle;
			uint32_t roughmetalTextureHandle;
			// Stores flags to determine which textures are present
			uint32_t textureMapFlag;
		};
		std::string modelName = "";
		std::vector<Submesh> submeshes{};
	};

	struct WireframeComponent : ModelComponent {};
	struct CollisionModelComponent : ModelComponent {
		glm::vec3 collisionScale = { 1.0,1.0,1.0 };
	};

	//All physics related calculations are done inside jolt system. This component is a container for the pointeres to the bodies.
	struct JoltPhysicsComponent {
		JPH::BodyID bodyID;
	};

	struct JoltKinematicComponent {
		enum MoveMode {
			//"Moves" to position over frameTime. Exerts force on contact
			PHYSICAL,
			//"Teleports" to position. Does not exert force on contact
			IMMEDIATE,
			//"Teleports" to position. Does not exert force on contact. Only moves if change is larger than some small value
			IMMEDIATE_OPTIMAL
		};

		JPH::BodyID bodyID;
		MoveMode moveMode = MoveMode::PHYSICAL;
	};

	struct InfoComponent {
		bool a;
	};
}