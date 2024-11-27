#pragma once
#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "bagel_textures.hpp"
#include "bagel_descriptors.hpp"
#include "bagel_ecs_components.hpp"

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

#include "bagel_util.hpp"


namespace bagel {
	// Magic hash function from boost, modified to return same value for same vertex
	inline void Hash(std::size_t& seed, const float& v)
	{
		//std::hash<T> hasher;
		seed ^= static_cast<uint32_t>(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}

	struct Material {
		std::string name;
		uint32_t albedoMap = 0;
		uint32_t normalMap = 0;
		uint32_t roughMap = 0;
		uint32_t metallicMap = 0;
		uint32_t specularMap = 0;
		uint32_t heightMap = 0;
		uint32_t opacityMap = 0;
		uint32_t aoMap = 0;
		uint32_t refractionMap = 0;
		uint32_t emissionMap = 0;
	};

	namespace BGLModel 
	{
		struct Vertex {
			glm::vec3 position;
			glm::vec3 color;
			glm::vec3 normal;
			glm::vec3 tangent{ 0.0f,0.0f,0.0f };
			glm::vec3 bitangent{ 0.0f,0.0f,0.0f };
			glm::vec2 uv;

			// texture indices
			// 0 index means unused
			uint32_t albedoMap = 0;
			uint32_t normalMap = 0;
			uint32_t roughMap = 0;
			uint32_t metallicMap = 0;
			uint32_t specularMap = 0;
			uint32_t heightMap = 0;
			uint32_t opacityMap = 0;
			uint32_t aoMap = 0;
			uint32_t refractionMap = 0;
			uint32_t emissionMap = 0;
			
			static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
			static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

			bool Vertex::operator=(const Vertex& other) const {
				return std::tie(
					position.x,position.y,position.z,
					normal.x,normal.y,normal.z) 
					== std::tie(other.position.x, other.position.y, other.position.z,
						other.normal.x, other.normal.y, other.normal.z);
			}
			bool Vertex::operator<(const Vertex& other) const {
				return std::tie(
					position.x, position.y, position.z,
					normal.x, normal.y, normal.z)
					< std::tie(other.position.x, other.position.y, other.position.z,
						other.normal.x, other.normal.y, other.normal.z);
			}
			bool Vertex::operator>(const Vertex& other) const {
				return std::tie(
					position.x, position.y, position.z,
					normal.x, normal.y, normal.z)
					> std::tie(other.position.x, other.position.y, other.position.z,
						other.normal.x, other.normal.y, other.normal.z);
			}
			
		};
		
		struct VertexHasher
		{
			size_t operator()(const Vertex& a) const
			{
				size_t seed{};
				Hash(seed, a.position.x);
				Hash(seed, a.position.y);
				Hash(seed, a.position.z);
				Hash(seed, a.normal.x);
				Hash(seed, a.normal.y);
				Hash(seed, a.normal.z);
				return seed;
			}
		};

		//We assume that two vertices with same vertex normal and same uv coordinate at same position are same vertices 
		struct VertexEquals
		{
			bool operator()(const Vertex& a, const Vertex& b) const
			{
				return std::tie(
					a.position.x, a.position.y, a.position.z,
					a.normal.x, a.normal.y, a.normal.z,
					a.uv.x,a.uv.y)
					== std::tie(b.position.x, b.position.y, b.position.z,
						b.normal.x, b.normal.y, b.normal.z,
						b.uv.x, b.uv.y);
			}
		};
	};

#ifdef old
	class BGLModelBufferManager {
	public:
		struct BufferHandlePair {
			uint32_t vertexBufferHandle = 0;
			uint32_t indexBufferHandle = 0;
			uint32_t vertexCount = 0;
			uint32_t indexCount = 0;
		};
		BGLModelBufferManager() = default;
		~BGLModelBufferManager() {
			for (auto v : vertexBufferArray) {
				vkDestroyBuffer(BGLDevice::device(), v, nullptr);
			}
			for (auto vm : vertexBufferMemoryArray) {
				vkFreeMemory(BGLDevice::device(), vm, nullptr);
			}
			for (auto i : indexBufferArray) {
				vkDestroyBuffer(BGLDevice::device(), i, nullptr);
			}
			for (auto im : IndexBufferMemoryArray) {
				vkFreeMemory(BGLDevice::device(), im, nullptr);
			}
		}
		const bool CheckAllocationByModelName(const std::string& modelName) {
			auto it = modelNameMap.find(modelName);
			return it != modelNameMap.end();
		}
		const BufferHandlePair& GetModelHandle(const std::string& modelName) {
			auto it = modelNameMap.find(modelName);
			if (it != modelNameMap.end()) {
				return it->second;
			}
			throw("Model named " + modelName + " was not allocated by ModelDescriptionComponentBuilder!");
		}
		const VkBuffer GetVertexBufferHandle(const BufferHandlePair& pair) {
			return vertexBufferArray[pair.vertexBufferHandle];
		}
		const VkBuffer GetIndexBufferHandle(const BufferHandlePair& pair) {
			return indexBufferArray[pair.indexBufferHandle];
		}
		// Returns last allocated vertex buffer
		VkBuffer& GetAllocatedVertexBuffer() {
			return vertexBufferArray[vertexBufferArray.size() - 1];
		}
		VkBuffer& GetVertexBufferDst() {
			int i = vertexBufferArray.size();
			VkBuffer newBuffer;
			vertexBufferArray.push_back(newBuffer);
			return vertexBufferArray[i];
		}
		VkDeviceMemory& GetVertexMemoryDst() {
			int i = vertexBufferArray.size();
			VkDeviceMemory newMem;
			vertexBufferMemoryArray.push_back(newMem);
			return vertexBufferMemoryArray[i];
		}
		// Returns last allocated index buffer
		VkBuffer& GetAllocatedIndexBuffer() {
			return indexBufferArray[indexBufferArray.size() - 1];
		}
		VkBuffer& GetIndexBufferDst() {
			int i = indexBufferArray.size();
			VkBuffer newBuffer;
			indexBufferArray.push_back(newBuffer);
			return indexBufferArray[i];
		}
		VkDeviceMemory& GetIndexMemoryDst() {
			int i = IndexBufferMemoryArray.size();
			VkDeviceMemory newMem;
			IndexBufferMemoryArray.push_back(newMem);
			return IndexBufferMemoryArray[i];
		}
		bool HasIndexBuffer(const BufferHandlePair& pair) {
			return pair.indexBufferHandle >= 0;
		}
		void EmplaceAllocatedModelVertexOnly(const std::string modelName, uint32_t vertexCount) {
			BufferHandlePair pair{};
			pair.vertexBufferHandle = vertexBufferArray.size() - 1;
			pair.vertexCount = vertexCount;
			modelNameMap.emplace(modelName, pair);
		}
		void EmplaceAllocatedModelAll(const std::string modelName, uint32_t vertexCount, uint32_t indexCount) {
			BufferHandlePair pair{};
			pair.vertexBufferHandle = vertexBufferArray.size() - 1;
			pair.vertexCount = vertexCount;
			pair.indexBufferHandle = indexBufferArray.size() - 1;
			pair.indexCount = indexCount;
			modelNameMap.emplace(modelName, pair);
		}

	private:
		std::unordered_map<std::string, BufferHandlePair> modelNameMap;
		std::vector<VkBuffer>		vertexBufferArray;
		std::vector<VkDeviceMemory> vertexBufferMemoryArray;
		std::vector<VkBuffer>		indexBufferArray;
		std::vector<VkDeviceMemory>	IndexBufferMemoryArray;
	};
#endif
	//Model-related Component Build Mode
	//Determines the vertex buffer layout
	enum ComponentBuildMode {
		FACES,
		LINES
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
		// Copied from main branch: Attemping to load GLTF and getting normal data can cause error because gltf loading does not save info to normalDataVertices
		// ComponentBuildMode::LINES is for wireframe rendering
		// ComponentBuildMode::FACES is for pbr rendering
		WireframeComponent& getNormalDataAsWireframe(entt::entity targetEnt) {
			assert(saveNextNormalData == true && "Save next normal data not set, can not retrieve normal data");
			saveNextNormalData = false;
			WireframeComponent& comp = registry.emplace<WireframeComponent>(targetEnt);
			createVertexBufferInputData(sizeof(BGLModel::Vertex) * normalDataVertices.size(), (void*) normalDataVertices.data(), comp.vertexBuffer, comp.vertexMemory);

			ModelComponent::Submesh sm{};
			comp.submeshes.push_back(sm);
			comp.vertexCount = normalDataVertices.size();
			
			normalDataVertices.clear();
			return comp;
		}

		template<typename T>
		T& buildComponent(entt::entity targetEnt, const char* modelFileName, ComponentBuildMode buildmode)
		{
			//Check through registry if the model was already loaded by another entity
			auto& regView = registry.view<T>();
			for (auto [entity, comp] : regView.each()) {
				//Already loaded. Copy over vk handles and mark as duplicate
				if (comp.modelName == std::string(modelFileName))
				{
					T& newComp = registry.emplace<T>(targetEnt);
					newComp.modelName = std::string(modelFileName);
					newComp.origin = &comp;
					comp.origin = &comp;

					//copy over everything
					for (ModelComponent::Submesh& const submesh : comp.submeshes) {
						newComp.submeshes.push_back(submesh);
					}
					newComp.vertexBuffer = comp.vertexBuffer;
					newComp.indexBuffer = comp.indexBuffer;
					newComp.vertexMemory = comp.vertexMemory;
					newComp.indexMemory = comp.indexMemory;

					return newComp;
				}
			}
			T& comp = registry.emplace<T>(targetEnt);

			loadModel(modelFileName, buildmode == LINES);

			//Not supported in lines mode. There is no concept of face in lines mode and vertex array can be smaller than 3
			if(buildmode != LINES) calculateTangent();

			createVertexBuffer(sizeof(BGLModel::Vertex) * vertices.size(), comp.vertexBuffer, comp.vertexMemory);
			if (indices.size() > 0) {
				std::cout << "Model has Index Buffer. Allocating...\n";
				createIndexBuffer(sizeof(uint32_t) * indices.size(), comp.indexBuffer, comp.indexMemory);
			}
			for (auto smi : submeshes) {
				ModelComponent::Submesh sm{};
				sm.firstIndex = smi.firstIndex;
				sm.indexCount = smi.indexCount;
				sm.materialIndex = smi.materialIndex;
				comp.submeshes.push_back(sm);
			}
			comp.indexCount = indices.size();
			comp.vertexCount = vertices.size();
			submeshes.clear();
			vertices.clear();
			indices.clear();
			std::cout << "Finished building Component\n";
			return comp;
		}

		void configureModelMaterialSet(std::vector<Material>* set);
		void removeModelMaterialSet() { p_materialSet = nullptr; }
	protected:
		struct SubmeshInfo {
			uint32_t firstIndex;
			uint32_t indexCount;
			uint32_t materialIndex;
			//For creating TransparentModelComponent as part of this entity
			bool transparentMaterial = false;
		};
		void loadGLTFModel(const char* filename);
		void loadGLTFMesh(tinygltf::Model model, tinygltf::Mesh mesh);
		void loadOBJModel(const char* filename, bool loadLines);
		void loadModel(const char* filename, bool loadLines);
		void generateGrid(int size);
		void calculateTangent();
		//Creates vertex buffer with any given set of BGLModel::Vertex
		void createVertexBufferInputData(size_t bufferSize, void* bufferSrc, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);
		//Creates vertex buffer with generated vertices
		void createVertexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);
		void createIndexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);
		BGLDevice& bglDevice;
		
		bool saveNextNormalData = false;

		std::vector<SubmeshInfo> submeshes{};
		std::vector<BGLModel::Vertex> vertices{};
		std::vector<uint32_t> indices{};

		std::vector<BGLModel::Vertex> normalDataVertices{};

		std::vector<Material>* p_materialSet = nullptr;

		entt::registry& registry;
	};
}