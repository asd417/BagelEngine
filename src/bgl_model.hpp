#pragma once
#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"

// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS

// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <memory>
#include <vector>
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
		};
		
		template<typename T>
		//Models can be built with either uint16_t index buffer or with uint32_t index buffer. For model with more than 65,535 vertices, use uint32_t
		struct Builder {
			std::vector<Vertex> vertices{};
			std::vector<T> indices{};
			void loadModel(const std::string& filename, uint32_t materialIndex);
		};

		BGLModel(BGLDevice& device, const BGLModel::Builder<uint16_t>& builder);
		BGLModel(BGLDevice& device, const BGLModel::Builder<uint32_t>& builder);
		~BGLModel();

		BGLModel(const BGLModel&) = delete;
		BGLModel& operator=(const BGLModel&) = delete;

		static std::unique_ptr<BGLModel> createModelFromFile(BGLDevice& device, const std::string& filepath, uint32_t textureIndex);

		void bind(VkCommandBuffer commandBuffer);
		void draw(VkCommandBuffer commandBuffer);
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

		void createVertexBuffers(const std::vector<Vertex>& vertices);
		template<typename T>
		void createIndexBuffers(const std::vector<T>& indices);

	};
}