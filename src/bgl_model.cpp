#include "bgl_model.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

//lib
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>


//https://www.youtube.com/watch?v=mnKp501RXDc&list=PL8327DO66nu9qYVKLDmdLW_84-yE4auCR&index=8&ab_channel=BrendanGalea
namespace bagel {
	BGLModel::BGLModel(BGLDevice& device, const BGLModel::Builder<uint16_t>& builder) : bglDevice{device}
	{
		createVertexBuffers(builder.vertices);
		createIndexBuffers<uint16_t>(builder.indices);
		useUint16 = true;
	}
	BGLModel::BGLModel(BGLDevice& device, const BGLModel::Builder<uint32_t>& builder) : bglDevice{ device }
	{
		createVertexBuffers(builder.vertices);
		createIndexBuffers<uint32_t>(builder.indices);
	}

	BGLModel::~BGLModel()
	{
	}

	void BGLModel::draw(VkCommandBuffer commandBuffer)
	{
		if (hasIndexBuffer) {
			vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
		}
		else {
			vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
		}
	}

	std::unique_ptr<BGLModel> BGLModel::createModelFromFile(BGLDevice& device, const std::string& filepath, uint32_t textureIndex)
	{
		Builder<uint32_t> builder{};
		builder.loadModel(filepath, textureIndex);
		std::cout << "Loading Model: " << filepath << "\n";
		std::cout << "Vertex Count: " << builder.vertices.size() << "\n";
		return std::make_unique<BGLModel>(device, builder);
	}

	void BGLModel::bind(VkCommandBuffer commandBuffer)
	{
		VkBuffer buffers[] = { vertexBuffer->getBuffer() };
		
		VkDeviceSize offsets[] = { 0 };
		//this function does not actually draw but records to command buffer to bind 1 vertex buffer starting at binding 0 with an offset of 0
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
		if (hasIndexBuffer) {
			if (useUint16) vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT16);
			else vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
		}
	}

	void BGLModel::createVertexBuffers(const std::vector<Vertex>& vertices)
	{
		vertexCount = static_cast<uint32_t>(vertices.size());
		assert(vertexCount >= 3 && "Vertex count must be at least 3");
		VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
		uint32_t vertexSize = sizeof(vertices[0]);
		// Staging buffer is good for optimizing data.
		// But if frequent update the the data is needed then the copyBuffer() may negate the performance gains
		// Memory barrier can be used for further optimization

		BGLBuffer stagingBuffer{
			bglDevice,
			vertexSize,
			vertexCount,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		};
		// HOST_VISIBLE_BIT we want the allocated memory to be accessible from the host(cpu) Necessary for host to be able to write to the device memory
		// COHERENT_BIT keeps the host and device memory regions consistent with each other. Without this property changes we make on the host memory will not be reflected to device memory
		// Creates a region of host memory mapped to device memory and stores the beginning pointer to 'data'
		// Due to COHERENT_BIT flag, the buffer will automatically be flushed to device memory after this memcpy
		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)vertices.data());
		
		vertexBuffer = std::make_unique<BGLBuffer>(
			bglDevice,
			vertexSize,
			vertexCount,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		// Finished Mapping vertex buffer to staging buffer inside device
		bglDevice.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
		// Finished Mapping device staging buffer to device vertex buffer. Destroy staging buffer. Staging buffer is a stack variable that gets cleaned up automatically

	}
	template<typename T>
	void BGLModel::createIndexBuffers(const std::vector<T>& indices)
	{
		indexCount = static_cast<T>(indices.size());
		hasIndexBuffer = indexCount > 0;
		if (!hasIndexBuffer) return;
		VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
		uint32_t indexSize = sizeof(indices[0]);

		BGLBuffer stagingBuffer{
			bglDevice,
			indexSize,
			indexCount,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		};

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)indices.data());

		indexBuffer = std::make_unique<BGLBuffer>(
			bglDevice,
			indexSize,
			indexCount,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		bglDevice.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
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
	template<typename T>
	void BGLModel::Builder<T>::loadModel(const std::string& filename, uint32_t textureIndex) {
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, error;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &error, filename.c_str())) {
			throw std::runtime_error(warn + error);
		}

		vertices.clear();
		indices.clear();
		for (const auto& shape : shapes) {
			for (const auto& index : shape.mesh.indices) {
				Vertex vertex{};
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
				vertex.texture_index = textureIndex;
				vertices.push_back(vertex);
			}
		}
	}

}