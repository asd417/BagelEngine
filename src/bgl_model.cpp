#include "bgl_model.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <array>
#include <map>

#include "bagel_engine_device.hpp"
#include "bagel_util.hpp"
//lib


#define TINYGLTF_IMPLEMENTATION
//#define TINYGLTF_NO_STB_IMAGE
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"
// #define TINYGLTF_NOEXCEPTION // optional. disable exception handling.

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
		attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,tangent) });
		attributeDescriptions.push_back({ 4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,bitangent) });
		attributeDescriptions.push_back({ 5, 0, VK_FORMAT_R32G32_SFLOAT,	offsetof(Vertex,uv) }); 
		
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
		entt::registry& _r) : 
		bglDevice{_bglDevice},
		registry{ _r }
	{}

	void ModelComponentBuilder::loadGLTFModel(const char* filename)
	{
		std::cout << "loading gfTF model " << filename << "\n";
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;
		bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, util::enginePath(filename));

		if (!warn.empty()) {
			printf("Warn: %s\n", warn.c_str());
		}

		if (!err.empty()) {
			printf("Err: %s\n", err.c_str());
		}

		if (!ret) {
			printf("Failed to parse glTF\n");
			throw("Failed to parse glTF");
		}

		std::cout << "\t" << filename << " has " << model.animations.size() << " animations\n";
		std::cout << "\t" << filename << " has " << model.meshes.size() << " meshes\n";

		//const tinygltf::Scene& scene = model.scenes[0];
		for (tinygltf::Mesh mesh :  model.meshes) {
			loadGLTFMesh(model, mesh);
		}

	}

	void ModelComponentBuilder::loadGLTFMesh(tinygltf::Model model, tinygltf::Mesh mesh)
	{
		//Reads all submeshes inside all meshes in the scene, effectively merging all meshes from the file into one model
		for (size_t i = 0; i < mesh.primitives.size(); i++) {
			SubmeshInfo smi{};
			uint32_t firstIndex = static_cast<uint32_t>(indices.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertices.size());
			const tinygltf::Primitive& glTFPrimitive = mesh.primitives[i];
			uint32_t indexCount = 0;
			{
				//Vertices
				const float* positionBuffer = nullptr;
				const float* normalsBuffer = nullptr;
				const float* texCoordsBuffer = nullptr;
				size_t vertexCount = 0;

				// Get buffer data for vertex positions
				if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[glTFPrimitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					positionBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					vertexCount = accessor.count;
				}
				// Get buffer data for vertex normals
				if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					normalsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				// Get buffer data for vertex texture coordinates
				// glTF supports multiple sets, we only load the first one
				if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					texCoordsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}

				// Append data to model's vertex buffer
				for (size_t v = 0; v < vertexCount; v++) {
					BGLModel::Vertex vert{};
					vert.position = glm::vec3(positionBuffer[v * 3], positionBuffer[v * 3 + 1], positionBuffer[v * 3 + 2]);
					vert.normal = glm::normalize(normalsBuffer ? glm::vec3(normalsBuffer[v * 3], normalsBuffer[v * 3 + 1], normalsBuffer[v * 3 + 2]) : glm::vec3(0.0f));
					vert.uv = texCoordsBuffer ? glm::vec2(texCoordsBuffer[v * 2], texCoordsBuffer[v * 2 + 1]) : glm::vec2(0.0f);
					vert.color = glm::vec3(1.0f);
					vertices.push_back(vert);
				}
			
				//Indices
				const tinygltf::Accessor& accessor = model.accessors[glTFPrimitive.indices];
				const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

				indexCount = static_cast<uint32_t>(accessor.count);

				// glTF supports different component types of indices
				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
					for (size_t index = 0; index < accessor.count; index++) {
						indices.push_back(buf[index] + vertexStart);
					}
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
					for (size_t index = 0; index < accessor.count; index++) {
						indices.push_back(static_cast<uint32_t>(buf[index]) + vertexStart);
					}
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					const uint8_t* buf = reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
					for (size_t index = 0; index < accessor.count; index++) {
						indices.push_back(static_cast<uint32_t>(buf[index]) + vertexStart);
					}
					break;
				}
				default:
					std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
					return;
				}
			}
			smi.firstIndex = firstIndex;
			smi.indexCount = indexCount;
			smi.materialIndex = glTFPrimitive.material;
			submeshes.push_back(smi);
		}
	}

	void ModelComponentBuilder::loadOBJModel(const char* filename, bool loadLines)
	{
		//For now, we do not use mtl files to load material info therefore we only make 1 submesh. let's keep it simple
		SubmeshInfo submesh{};

		std::string fullPath = util::enginePath(filename);
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, error;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &error, fullPath.c_str())) {
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
		else {
			//When loading model as wireframe
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
		submesh.firstIndex = 0;
		submesh.indexCount = indices.size();
		submeshes.push_back(submesh);
	}

	void ModelComponentBuilder::loadModel(const char* filename, bool loadLines) {
		//Load generated models here
		if (strcmp(filename, "grid")==0) {
			SubmeshInfo gridMesh{};
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
			gridMesh.firstIndex = 0;
			gridMesh.indexCount = indices.size();
			submeshes.push_back(gridMesh);
			return;
		}

		//Load model by filetype
		//Only requirement for each model-loading function is to separate model into submeshes by material
		// and populate std::vector<SubmeshInfo> submeshes 
		const char* filetype = strrchr(filename, '.');
		if (strcmp(filetype, ".gltf") == 0) {
			loadGLTFModel(filename); 
		}
		else if (strcmp(filetype, ".obj") == 0) {
			loadOBJModel(filename, loadLines);
		}
	}

	void ModelComponentBuilder::calculateTangent()
	{
		//At this point, vertices are stored in the vertices vector and indices are stored in the indices vector
		//using the indices vector, we visit each vertices per triangle.
		//Then we calculate tangent vector and average with existing calculated result
		if (indices.size() <= 0) {
			for (int i = 0; i < vertices.size(); i += 3) {
				glm::vec3 pos1 = vertices[i].position;
				glm::vec3 pos2 = vertices[i + 1].position;
				glm::vec3 pos3 = vertices[i + 2].position;

				glm::vec2 uv1 = vertices[i].uv;
				glm::vec2 uv2 = vertices[i + 1].uv;
				glm::vec2 uv3 = vertices[i + 2].uv;

				glm::vec3 edge1 = pos2 - pos1;
				glm::vec3 edge2 = pos3 - pos1;
				glm::vec2 deltaUV1 = uv2 - uv1;
				glm::vec2 deltaUV2 = uv3 - uv1;

				float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
				for (int vi = 0; vi < 3; vi++) {
					glm::vec3& tangent = vertices[i + vi].tangent;
					glm::vec3& bitangent = vertices[i + vi].bitangent;
					if (tangent.x == tangent.y == tangent.z == 0.0f) {
						//Dont average. This is the first time
						tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
					}
					else {
						//Average with existing tangent
						tangent.x += f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent.y += f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent.z += f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						bitangent.x += f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent.y += f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent.z += f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						tangent.x /= 2;
						tangent.y /= 2;
						tangent.z /= 2;
						bitangent.x /= 2;
						bitangent.y /= 2;
						bitangent.z /= 2;
					}
				}
			}
		}
		else {
			for (int i = 0; i < indices.size(); i += 3) {
			
				glm::vec3 pos1 = vertices[indices[ i ]].position;
				glm::vec3 pos2 = vertices[indices[ i + 1 ]].position;
				glm::vec3 pos3 = vertices[indices[ i + 2 ]].position;

				glm::vec2 uv1 = vertices[indices[ i ]].uv;
				glm::vec2 uv2 = vertices[indices[ i + 1]].uv;
				glm::vec2 uv3 = vertices[indices[ i + 2]].uv;

				glm::vec3 edge1 = pos2 - pos1;
				glm::vec3 edge2 = pos3 - pos1;
				glm::vec2 deltaUV1 = uv2 - uv1;
				glm::vec2 deltaUV2 = uv3 - uv1;

				float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
				for (int vi = 0; vi < 3;vi++) {	
					glm::vec3& tangent = vertices[indices[i+vi]].tangent;
					glm::vec3& bitangent = vertices[indices[i + vi]].bitangent;
					if (tangent.x == tangent.y == tangent.z == 0.0f) {
						//Dont average. This is the first time
						tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
					}
					else {
						//Average with existing tangent
						tangent.x += f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent.y += f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent.z += f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						bitangent.x += f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent.y += f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent.z += f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						tangent.x /= 2;
						tangent.y /= 2;
						tangent.z /= 2;
						bitangent.x /= 2;
						bitangent.y /= 2;
						bitangent.z /= 2;
					}
				}
			}
		}
	}

	void ModelComponentBuilder::createVertexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst)
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

	void ModelComponentBuilder::createIndexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst)
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