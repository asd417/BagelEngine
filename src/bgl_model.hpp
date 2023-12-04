#pragma once
#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "bagel_textures.h"
#include "bagel_descriptors.hpp"
#include "bagel_ecs_components.hpp"

// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <memory>
#include <vector>
#include <tuple>
#include <ostream>
namespace bagel {

	// The purpose of this class is to be able to take vertex data on cpu, allocate memory and copy it over to gpu device
	class BGLModel {
	public:
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
		
		template<typename T>
		//Models can be built with either uint16_t index buffer or with uint32_t index buffer. For model with more than 65,535 vertices, use uint32_t
		struct Builder {
			std::vector<Vertex> vertices{};
			std::vector<T> indices{};
			void loadModel(const std::string& filename, uint32_t materialIndex);
		};

		

		BGLModel(BGLDevice& device, const BGLModel::Builder<uint16_t>& builder);
		BGLModel::BGLModel(
			BGLDevice& device, 
			const BGLModel::Builder<uint32_t>& builder, 
			std::string textureFilePath, 
			std::unique_ptr<BGLDescriptorSetLayout>& modelSetLayout,
			BGLDescriptorPool& globalPool);
		~BGLModel();

		BGLModel(const BGLModel&) = delete;
		BGLModel& operator=(const BGLModel&) = delete;

		static std::unique_ptr<BGLModel> BGLModel::createModelFromFile(
			BGLDevice& device, 
			const std::string& filepath, 
			const std::string& textureFilePath,
			std::unique_ptr<BGLDescriptorSetLayout>& modelSetLayout,
			BGLDescriptorPool& globalPool);

		void bind(VkCommandBuffer commandBuffer);
		void draw(VkCommandBuffer commandBuffer);
		VkDescriptorSet getTextureDescriptorSet() const { return modelDescriptorSet; }
	private:
		BGLDevice& bglDevice;
		bool hasIndexBuffer = false;
		bool useUint16 = false;

		//VkBuffer vertexBuffer;
		//VkDeviceMemory vertexBufferMemory;
		std::unique_ptr<BGLBuffer> vertexBuffer;
		uint32_t vertexCount;

		//VkBuffer indexBuffer;
		//VkDeviceMemory indexBufferMemory;
		std::unique_ptr<BGLBuffer> indexBuffer;
		uint32_t indexCount;

		VkDescriptorSet modelDescriptorSet;
		std::unique_ptr<BGLTexture> modelTexture;

		void createVertexBuffers(const std::vector<Vertex>& vertices);
		template<typename T>
		void createIndexBuffers(const std::vector<T>& indices);

	};

	class ModelDescriptionComponentBuilder {
	public:
		ModelDescriptionComponentBuilder(BGLDevice& _device);
		void setBuildTarget(ModelDescriptionComponent* _tC) { targetComponent = _tC; }
		void buildComponent(const std::string& filename);

	private:
		void loadModel(const std::string& filename);
		void printVertexArray();
		void printIndexArray();
		void createVertexBuffer();
		void createIndexBuffer();

		ModelDescriptionComponent* targetComponent = nullptr;
		BGLDevice& bglDevice;

		std::vector<BGLModel::Vertex> vertices{};
		std::vector<uint32_t> indices{};
	};
}