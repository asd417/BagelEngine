#include "bagel_model.hpp"
#include "model_loaders/generated.hpp"
#include "model_loaders/obj.hpp"
#include "model_loaders/gltf.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include <cassert>
#include <cstring>
#include <cmath>
#include <iostream>
#include <array>
#include <map>
#include <unordered_map>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "bagel_engine_device.hpp"

// lib

#include "bagel_imgui.hpp"
#define CONSOLE ConsoleApp::Instance()

namespace bagel
{

	std::ostream &operator<<(std::ostream &os, const BGLModel::Vertex &other)
	{
		return os << "P " << other.position.x << "\t" << other.position.y << "\t" << other.position.z << "\tN " << other.normal.x << "\t" << other.normal.y << "\t" << other.normal.z << " C " << other.color.x << "\t" << other.color.y << "\t" << other.color.z << "\n";
	}

	std::vector<VkVertexInputBindingDescription> BGLModel::Vertex::getBindingDescriptions()
	{
		// Possible to write this in brace construction {{0, sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}}
		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
		bindingDescriptions[0].binding = 0;
		bindingDescriptions[0].stride = sizeof(Vertex); // This allows easier addition of attribute as the stride will automatically adjusted.
		bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return bindingDescriptions;
	}

	std::vector<VkVertexInputAttributeDescription> BGLModel::Vertex::getAttributeDescriptions()
	{
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

		// We want both color and vertex in one binding so the binding is kept 0
		attributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)}); // location, binding, format, offset
		attributeDescriptions.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)});
		attributeDescriptions.push_back({2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
		attributeDescriptions.push_back({3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)});
		attributeDescriptions.push_back({4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});
		attributeDescriptions.push_back({5, 0, VK_FORMAT_R16_UINT, offsetof(Vertex, albedoMap)});
		attributeDescriptions.push_back({6, 0, VK_FORMAT_R16_UINT, offsetof(Vertex, normalMap)});
		attributeDescriptions.push_back({7,  0, VK_FORMAT_R16_UINT, offsetof(Vertex, metalRoughMap)});
		attributeDescriptions.push_back({8,  0, VK_FORMAT_R16_UINT, offsetof(Vertex, specularMap)});
		attributeDescriptions.push_back({9,  0, VK_FORMAT_R16_UINT, offsetof(Vertex, heightMap)});
		attributeDescriptions.push_back({10, 0, VK_FORMAT_R16_UINT, offsetof(Vertex, opacityMap)});
		attributeDescriptions.push_back({11, 0, VK_FORMAT_R16_UINT, offsetof(Vertex, aoMap)});
		attributeDescriptions.push_back({12, 0, VK_FORMAT_R16_UINT, offsetof(Vertex, refractionMap)});
		attributeDescriptions.push_back({13, 0, VK_FORMAT_R16_UINT, offsetof(Vertex, emissionMap)});
		// offsetof macro calculates the byte offset of color member in the Vertex struct

		return attributeDescriptions;
	}

	// Builder class of all Model-loading components
	// Any component with following members is valid:
	//  std::string modelName;
	//  uint32_t vertexCount;
	//  uint32_t indexCount;

	ModelComponentBuilder::ModelComponentBuilder(
		BGLDevice &_bglDevice,
		entt::registry &_r) : bglDevice{_bglDevice},
							  registry{_r}
	{
	}
	void ModelComponentBuilder::configureModelMaterialSet(std::vector<GLTFMaterial> *set)
	{
		if (p_materialSet == nullptr)
			p_materialSet = set;
		else
			std::cout << "ModelComponentBuilder::configureModelMaterialSet() Could not configure material set, remove existing material set first.";
	}
	
	void ModelComponentBuilder::loadModel(const char *filename, ModelLoadSettings buildSettings)
	{
		// Intercept well-known names as procedural geometry
		const char* genName = nullptr;
		if      (strcmp(filename, "grid")                 == 0) genName = "grid";
		else if (strcmp(filename, "/models/cube.obj")     == 0) genName = "cube";
		else if (strcmp(filename, "/models/floor.obj")    == 0) genName = "floor";
		else if (strcmp(filename, "/models/sphere.obj")   == 0) genName = "sphere";
		else if (strcmp(filename, "/models/icosphere.obj")== 0) genName = "icosphere";
		else if (strcmp(filename, "/models/wirecube.obj") == 0) genName = "wirecube";
		else if (strcmp(filename, "/models/wiresphere.obj")== 0) genName = "wiresphere";
		else if (strcmp(filename, "/models/axis.obj")     == 0) genName = "axis";

		if (genName) {
			buildSettings.source = genName;
			activeLoader = std::make_unique<GeneratedModelLoader>();
		} else {
			buildSettings.source = filename;
			const char *ext = strrchr(filename, '.');
			if (ext && strcmp(ext, ".gltf") == 0) {
				activeLoader = std::make_unique<GLTFModelLoader>(pTextureLoader);
			} else if (ext && strcmp(ext, ".obj") == 0) {
				activeLoader = std::make_unique<OBJModelLoader>(pTextureLoader);
			} else {
				std::cerr << "[ModelComponentBuilder] Unknown file type: " << filename << "\n";
				return;
			}
		}
		activeLoader->load(buildSettings);
	}


	void ModelComponentBuilder::createVertexBufferInputData(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst)
	{
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;
		void *mapped;
		bglDevice.createBuffer(
			bufferSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingMemory);
		vkMapMemory(BGLDevice::device(), stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
		// Write Vertex data to stagingBuffer
		assert(mapped && "Cannot copy to unmapped buffer");
		memcpy(mapped, bufferSrc, bufferSize);

		bglDevice.createBuffer(
			bufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			bufferDst,
			memoryDst);

		// Finished Mapping vertex buffer to staging buffer inside device
		bglDevice.copyBuffer(stagingBuffer, bufferDst, bufferSize);

		// Finished Mapping device staging buffer to device vertex buffer. Destroy staging buffer.
		vkUnmapMemory(BGLDevice::device(), stagingMemory);
		vkDestroyBuffer(BGLDevice::device(), stagingBuffer, nullptr);
		vkFreeMemory(BGLDevice::device(), stagingMemory, nullptr);
		mapped = nullptr;
	}

	void ModelComponentBuilder::createVertexBuffer(size_t bufferSize, VkBuffer &bufferDst, VkDeviceMemory &memoryDst)
	{
		createVertexBufferInputData(bufferSize, (void *)activeLoader->getVertices().data(), bufferDst, memoryDst);
	}

	void ModelComponentBuilder::createIndexBuffer(size_t bufferSize, VkBuffer &bufferDst, VkDeviceMemory &memoryDst)
	{
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;
		void *mapped;
		bglDevice.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);
		vkMapMemory(BGLDevice::device(), stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
		// Write Index data to stagingBuffer
		std::cout << "Writing Index data to stagingBuffer\n";
		assert(mapped && "Cannot copy to unmapped buffer");
		memcpy(mapped, (void *)activeLoader->getIndices().data(), bufferSize);

		bglDevice.createBuffer(bufferSize,
							   VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							   bufferDst,
							   memoryDst);

		// Map index buffer to staging buffer inside device
		bglDevice.copyBuffer(stagingBuffer, bufferDst, bufferSize);

		// Finished Mapping device staging buffer to device index buffer due to VK_BUFFER_USAGE_TRANSFER_DST_BIT. Destroy staging buffer.
		vkUnmapMemory(BGLDevice::device(), stagingMemory);
		vkDestroyBuffer(BGLDevice::device(), stagingBuffer, nullptr);
		vkFreeMemory(BGLDevice::device(), stagingMemory, nullptr);
		mapped = nullptr;
	}

}