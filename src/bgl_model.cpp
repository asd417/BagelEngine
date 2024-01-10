#include "bgl_model.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <array>
#include <map>

#include "bagel_engine_device.hpp"

//lib
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

// There are hard-coded behaviors:
// WireframeComponentBuilder::buildComponent("grid") : generates a 50x50 square grid

namespace bagel {

	std::ostream& operator<<(std::ostream& os, const BGLModel::Vertex& other) {
		return os << "P " << other.position.x << "\t" << other.position.y << "\t" << other.position.z << "\tN " << other.normal.x << "\t" << other.normal.y << "\t" << other.normal.z << " C " << other.color.x << "\t" << other.color.y << "\t" << other.color.z << "\n";
	}

	std::vector<VkVertexInputBindingDescription> BGLModel::Vertex::getBindingDescriptions()
	{
		//Possible to write this in brace construction {{0, sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}}
		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
		bindingDescriptions[0].binding = 0;
		bindingDescriptions[0].stride = sizeof(Vertex); //This allows easier addition of attribute as the stride will automatically adjusted.
		bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return bindingDescriptions;
	}

	std::vector<VkVertexInputAttributeDescription> BGLModel::Vertex::getAttributeDescriptions()
	{
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

		//We want both color and vertex in one binding so the binding is kept 0
		attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,position) }); //location, binding, format, offset
		attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,color) });
		attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,normal) });
		attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT,	offsetof(Vertex,uv) }); 
		attributeDescriptions.push_back({ 4, 0, VK_FORMAT_R32_SINT ,	offsetof(Vertex,texture_index) });
		
		//offsetof macro calculates the byte offset of color member in the Vertex struct

		return attributeDescriptions;
	}

	//Builder class of all Model-loading components
	//Any component with following members is valid:
	// std::string modelName;
	// uint32_t vertexCount;
	// uint32_t indexCount;

	ModelComponentBuilder::ModelComponentBuilder(
		BGLDevice& _bglDevice, 
		const std::unique_ptr<BGLModelBufferManager>& _modelBufferManager) : 
		bglDevice{_bglDevice},
		modelBufferManager{ _modelBufferManager }
	{}

	// ComponentBuildMode::LINES is for wireframe rendering
	// ComponentBuildMode::FACES is for pbr rendering
	void ModelComponentBuilder::buildComponent(const std::string& modelFilename, ComponentBuildMode buildmode, std::string& outModelName, uint32_t& outVertexCount, uint32_t& outIndexCount)
	{
		outModelName = modelFilename;
		if (modelBufferManager->CheckAllocationByModelName(modelFilename)) {
			const BGLModelBufferManager::BufferHandlePair& handles = modelBufferManager->GetModelHandle(modelFilename);
			outVertexCount = handles.vertexCount;
			outIndexCount = handles.indexCount;
			return;
		}
		loadModel(modelFilename, buildmode == LINES);
		outVertexCount = static_cast<uint32_t>(vertices.size());
		createVertexBuffer(sizeof(vertices[0]) * vertices.size());
		if (indices.size() > 0) {
			std::cout << "Model has Index Buffer. Allocating...\n";
			outIndexCount = static_cast<uint32_t>(indices.size());
			std::cout << "Index Buffer length: " << outIndexCount << "\n";
			createIndexBuffer(sizeof(indices[0]) * indices.size());
			modelBufferManager->EmplaceAllocatedModelAll(modelFilename, outVertexCount, outIndexCount);
		}
		else {
			modelBufferManager->EmplaceAllocatedModelVertexOnly(modelFilename, outVertexCount);
		}
		vertices.clear();
		indices.clear();
		std::cout << "Finished building Component\n";
	}

	void ModelComponentBuilder::loadModel(const std::string& filename, bool loadLines) {
		if (filename == "grid") {
			for (int i = 0; i < 101; i++) {
				BGLModel::Vertex vertex1{};
				vertex1.position = { i - 50, 0, -50 };
				vertices.push_back(vertex1);
				BGLModel::Vertex vertex2{};
				vertex2.position = { i - 50, 0, 50 };
				vertices.push_back(vertex2);

				BGLModel::Vertex vertex4{};
				vertex4.position = { -50, 0, i - 50 };
				vertices.push_back(vertex4);
				BGLModel::Vertex vertex5{};
				vertex5.position = { 50, 0, i - 50 };
				vertices.push_back(vertex5);
			}
			return;
		}
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, error;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &error, filename.c_str())) {
			throw std::runtime_error(warn + error);
		}
		std::unordered_map<BGLModel::Vertex, uint32_t, BGLModel::VertexHasher, BGLModel::VertexEquals> vertexMap{};
		uint32_t vertInt = 0;
		if (!loadLines) {
			for (const auto& shape : shapes) {
				for (const auto& index : shape.mesh.indices) {
					BGLModel::Vertex vertex{};
					if (index.vertex_index >= 0) {
						vertex.position = { attrib.vertices[3 * index.vertex_index + 0] , attrib.vertices[3 * index.vertex_index + 1] , attrib.vertices[3 * index.vertex_index + 2] };
					}

					vertex.color = { attrib.colors[3 * index.vertex_index] , attrib.colors[3 * index.vertex_index + 1] , attrib.colors[3 * index.vertex_index + 2] };

					if (index.normal_index >= 0) {
						vertex.normal = { attrib.normals[3 * index.normal_index + 0] , attrib.normals[3 * index.normal_index + 1] , attrib.normals[3 * index.normal_index + 2] };
					}
					if (index.texcoord_index >= 0) {
						vertex.uv = { attrib.texcoords[2 * index.texcoord_index + 0] , 1 - attrib.texcoords[2 * index.texcoord_index + 1] };
					}
					int index;
					if (auto search = vertexMap.find(vertex); search != vertexMap.end()) {
						//vertex already exists in map
						index = search->second;
					} else {
						//new vertex
						vertexMap.emplace(vertex, vertInt);
						index = vertInt;
						vertInt++;
						vertices.push_back(vertex);
					}
					//vertices.push_back(vertex);
					indices.push_back(index);
				}
				for (const auto& mat : shape.mesh.material_ids) {

				}
			}
		}
		else {
			for (const auto& shape : shapes) {
				for (const auto& index : shape.lines.indices) {
					BGLModel::Vertex vertex{};
					if (index.vertex_index >= 0) {
						vertex.position = { attrib.vertices[3 * index.vertex_index + 0] , attrib.vertices[3 * index.vertex_index + 1] , attrib.vertices[3 * index.vertex_index + 2] };
					}

					vertex.color = { attrib.colors[3 * index.vertex_index] , attrib.colors[3 * index.vertex_index + 1] , attrib.colors[3 * index.vertex_index + 2] };

					if (index.normal_index >= 0) {
						vertex.normal = { attrib.normals[3 * index.normal_index + 0] , attrib.normals[3 * index.normal_index + 1] , attrib.normals[3 * index.normal_index + 2] };
					}
					if (index.texcoord_index >= 0) {
						vertex.uv = { attrib.texcoords[2 * index.texcoord_index + 0] , 1 - attrib.texcoords[2 * index.texcoord_index + 1] };
					}
					int index;
					if (auto search = vertexMap.find(vertex); search != vertexMap.end()) {
						//vertex already exists in map
						index = search->second;
					}
					else {
						//new vertex
						vertexMap.emplace(vertex, vertInt);
						index = vertInt;
						vertInt++;
						vertices.push_back(vertex);
					}
					//vertices.push_back(vertex);
					indices.push_back(index);
				}
			}
		}
		std::cout << "Model Loader looped through " << vertInt << "\n";
		std::cout << "Vertex Buffer size " << vertices.size() << "\n";
		std::cout << "Index Buffer size " << indices.size() << "\n";
	}

	void ModelComponentBuilder::printVertexArray() {
		std::cout << "[ ";
		for (auto i : vertices) {
			std::cout << i << ", ";
		}
		std::cout << "]\n";
	}

	void ModelComponentBuilder::printIndexArray() {
		std::cout << "[ ";
		for (auto i : indices) {
			std::cout << i << " ";
		}
		std::cout << "]\n";
	}

	void ModelComponentBuilder::createVertexBuffer(size_t bufferSize)
	{
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;
		void* mapped;
		bglDevice.createBuffer(
			bufferSize, 
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			stagingBuffer, 
			stagingMemory);
		vkMapMemory(BGLDevice::device(), stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
		//Write Vertex data to stagingBuffer
		assert(mapped && "Cannot copy to unmapped buffer");
		memcpy(mapped, (void*)vertices.data(), bufferSize);

		bglDevice.createBuffer(
			bufferSize, 
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
			modelBufferManager->GetVertexBufferDst(), 
			modelBufferManager->GetVertexMemoryDst());

		// Finished Mapping vertex buffer to staging buffer inside device
		bglDevice.copyBuffer(stagingBuffer, modelBufferManager->GetAllocatedVertexBuffer(), bufferSize);

		// Finished Mapping device staging buffer to device vertex buffer. Destroy staging buffer.
		vkUnmapMemory(BGLDevice::device(), stagingMemory);
		vkDestroyBuffer(BGLDevice::device(), stagingBuffer, nullptr);
		vkFreeMemory(BGLDevice::device(), stagingMemory, nullptr);
		mapped = nullptr;

	}

	void ModelComponentBuilder::createIndexBuffer(size_t bufferSize)
	{
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;
		void* mapped;
		bglDevice.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);
		vkMapMemory(BGLDevice::device(), stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
		//Write Index data to stagingBuffer
		std::cout << "Writing Index data to stagingBuffer\n";
		assert(mapped && "Cannot copy to unmapped buffer");
		memcpy(mapped, (void*)indices.data(), bufferSize);

		bglDevice.createBuffer(bufferSize, 
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
			modelBufferManager->GetIndexBufferDst(),
			modelBufferManager->GetIndexMemoryDst());

		// Map index buffer to staging buffer inside device
		bglDevice.copyBuffer(stagingBuffer, modelBufferManager->GetAllocatedIndexBuffer(), bufferSize);

		// Finished Mapping device staging buffer to device index buffer due to VK_BUFFER_USAGE_TRANSFER_DST_BIT. Destroy staging buffer.
		vkUnmapMemory(BGLDevice::device(), stagingMemory);
		vkDestroyBuffer(BGLDevice::device(), stagingBuffer, nullptr);
		vkFreeMemory(BGLDevice::device(), stagingMemory, nullptr);
		mapped = nullptr;
	}
}