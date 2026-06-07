#pragma once
#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "bagel_textures.hpp"
#include "bagel_descriptors.hpp"
#include "bagel_ecs_components.hpp"
#include "bagel_model_loader.hpp"

// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "tiny_gltf.h"
#include <memory>
#include <map>
#include <vector>
#include <tuple>
#include <ostream>
#include <functional>
#include <limits>

#include "bagel_util.hpp"

namespace bagel {
	struct GLTFMaterial {
		std::string name;
		uint16_t albedoMap = 0;
		uint16_t normalMap = 0;
		uint16_t metalRoughMap = 0;
		uint16_t specularMap = 0;
		uint16_t heightMap = 0;
		uint16_t opacityMap = 0;
		uint16_t aoMap = 0;
		uint16_t refractionMap = 0;
		uint16_t emissionMap = 0;
	};

	//Component builder for generic model components
	class ModelComponentBuilder {
	public:
		ModelComponentBuilder(BGLDevice& bglDevice, entt::registry& registry);
		void saveNormalData() { 
			assert(saveNextNormalData == false && "Already Set to save next normal data, retrieve existing data first");
			saveNextNormalData = true; 
		}

		//Using entt::entity and registry, this component builder will create components in the registry
		// As of 2024-09-25 Attemping to load GLTF and getting normal data can cause error because gltf loading does not save info to normalDataVertices
		// ComponentBuildMode::LINES is for wireframe rendering
		// ComponentBuildMode::FACES is for pbr rendering
		WireframeComponent& getNormalDataAsWireframe(entt::entity targetEnt) {
			assert(saveNextNormalData == true && "Save next normal data not set, can not retrieve normal data");
			saveNextNormalData = false;
			WireframeComponent& comp = registry.emplace<WireframeComponent>(targetEnt);
			createVertexBufferInputData(sizeof(BGLModel::Vertex) * normalDataVertices.size(), (void*) normalDataVertices.data(), comp.vertexBuffer, comp.vertexMemory);

			comp.submeshes[comp.submeshCount++] = ModelComponent::Submesh{};
			comp.vertexCount = static_cast<uint32_t>(normalDataVertices.size());
			
			normalDataVertices.clear();
			return comp;
		}

		template<typename T>
		T& buildComponent(entt::entity targetEnt, const char* modelFileName, ModelLoadSettings buildSettings)
		{
			//Check through registry if the model was already loaded by another entity
			auto regView = registry.view<T>();
			for (auto [entity, comp] : regView.each()) {
				//Already loaded. Copy over vk handles and mark as duplicate
				if (comp.modelName == std::string(modelFileName))
				{
					T& newComp = registry.emplace<T>(targetEnt);
					newComp.modelName = std::string(modelFileName);
					newComp.origin = &comp;
					comp.origin = &comp;

					newComp.submeshCount = comp.submeshCount;
					for (uint32_t _i = 0; _i < comp.submeshCount; _i++) {
						newComp.submeshes[_i] = comp.submeshes[_i];
						newComp.materials[_i] = comp.materials[_i];
					}
					newComp.vertexBuffer = comp.vertexBuffer;
					newComp.indexBuffer = comp.indexBuffer;
					newComp.vertexMemory = comp.vertexMemory;
					newComp.indexMemory = comp.indexMemory;
					newComp.aabbMin = comp.aabbMin;
					newComp.aabbMax = comp.aabbMax;
					newComp.frustumCull = comp.frustumCull;

					return newComp;
				}
			}
			T& comp = registry.emplace<T>(targetEnt);
			loadModel(modelFileName, buildSettings);

			//Not supported in lines mode. There is no concept of face in lines mode and vertex array can be smaller than 3
			if(buildSettings.buildMode != LINES && !activeLoader->hasTangents()) activeLoader->calculateTangent();

			auto& vertices  = activeLoader->getVertices();
			auto& indices   = activeLoader->getIndices();
			auto& submeshes = activeLoader->getSubmeshes();

			// Compute model-space AABB
			{
				glm::vec3 bMin( std::numeric_limits<float>::max());
				glm::vec3 bMax(-std::numeric_limits<float>::max());
				for (const auto& v : vertices) {
					bMin = glm::min(bMin, v.position);
					bMax = glm::max(bMax, v.position);
				}
				comp.aabbMin = bMin;
				comp.aabbMax = bMax;
			}

			std::cout << "Creating Vertex Buffer\n";
			createVertexBuffer(sizeof(BGLModel::Vertex) * vertices.size(), comp.vertexBuffer, comp.vertexMemory);
			if (indices.size() > 0) {
				std::cout << "Model has Index Buffer. Allocating...\n";
				createIndexBuffer(sizeof(uint32_t) * indices.size(), comp.indexBuffer, comp.indexMemory);
			}
			for (auto& smi : submeshes) {
				assert(comp.submeshCount < ModelComponent::MAX_SUBMESHES && "Exceeded MAX_SUBMESHES");
				ModelComponent::Submesh& sm = comp.submeshes[comp.submeshCount++];
				sm.firstIndex    = smi.firstIndex;
				sm.indexCount    = smi.indexCount;
				sm.firstVertex   = smi.firstVertex;
				sm.vertexCount   = smi.vertexCount;
				sm.materialIndex = smi.materialIndex;
				// Per-submesh AABB in model space
				glm::vec3 sMin( std::numeric_limits<float>::max());
				glm::vec3 sMax(-std::numeric_limits<float>::max());
				uint32_t vEnd = smi.firstVertex + smi.vertexCount;
				for (uint32_t vi = smi.firstVertex; vi < vEnd; vi++) {
					sMin = glm::min(sMin, vertices[vi].position);
					sMax = glm::max(sMax, vertices[vi].position);
				}
				sm.aabbMin = sMin;
				sm.aabbMax = sMax;
			}
			comp.indexCount  = static_cast<uint32_t>(indices.size());
			comp.vertexCount = static_cast<uint32_t>(vertices.size());
			activeLoader.reset();
			std::cout << "Finished building Component\n";
			return comp;
		}

		void configureModelMaterialSet(std::vector<GLTFMaterial>* set);

		void removeModelMaterialSet() { p_materialSet = nullptr; }

		// Optional: set before buildComponent() to enable texture loading for OBJ/GLTF models
		void setTextureLoader(BGLTextureLoader* tl) { pTextureLoader = tl; }
	protected:
		void loadModel(const char* filename, ModelLoadSettings buildSettings);

		//Creates vertex buffer with any given set of BGLModel::Vertex
		void createVertexBufferInputData(size_t bufferSize, void* bufferSrc, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);
		//Creates vertex buffer with generated vertices
		void createVertexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);
		void createIndexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);

		bool saveNextNormalData = false;

		std::unique_ptr<ModelLoaderBase> activeLoader;
		std::vector<BGLModel::Vertex> normalDataVertices{};
		std::vector<GLTFMaterial>* p_materialSet = nullptr;
		BGLTextureLoader* pTextureLoader = nullptr;

		BGLDevice& bglDevice;
		entt::registry& registry;
	};
}