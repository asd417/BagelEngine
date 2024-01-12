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
namespace bagel {
	// Magic hash function from boost, modified to return same value for same vertex
	inline void Hash(std::size_t& seed, const float& v)
	{
		//std::hash<T> hasher;
		seed ^= static_cast<uint32_t>(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}

	namespace BGLModel 
	{
		struct Vertex {
			glm::vec3 position;
			glm::vec3 color;
			glm::vec3 normal;
			glm::vec2 uv;
			uint32_t texture_index;

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

	//Component Build Mode
	enum ComponentBuildMode {
		FACES,
		LINES
	};
	//Component builder for generic model components
	class ModelComponentBuilder {
	public:
		ModelComponentBuilder(BGLDevice& bglDevice, const std::unique_ptr<BGLModelBufferManager>& modelBufferManager);
		virtual void buildComponent(const char* filename, ComponentBuildMode buildmode, std::string& outModelName, uint32_t& outVertexCount, uint32_t& outIndexCount);
		void loadGLTF(const char* filename);

	protected:
		virtual void loadModel(const char* filename, bool loadLines);
		void printVertexArray();
		void printIndexArray();
		void createVertexBuffer(size_t bufferSize);
		void createIndexBuffer(size_t bufferSize);
		BGLDevice& bglDevice;
		const std::unique_ptr<BGLModelBufferManager>& modelBufferManager;
		std::vector<BGLModel::Vertex> vertices{};
		std::vector<uint32_t> indices{};
	};
}