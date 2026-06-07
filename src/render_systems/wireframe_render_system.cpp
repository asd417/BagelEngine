#include "wireframe_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"
#include "../bagel_util.hpp"

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <array>
#include <chrono>
#include <iostream>
#include <cstring>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace bagel {
	// PushConstantData is a performant and simple way to send data to vertex and fragment shader
	// It is typically faster than descriptor sets for frequently updated data
	// 
	// But its size is limited to 128 bytes (technically each device has different max size but only 128 is guaranteed)
	// This maximum size varies from device to device and is specified in bytes by the max_push_constants_size field of vk::PhysicalDeviceLimits
	// Even the high end device (RTX3080) has only 256 bytes available so it is unrealistic to send most data
	// 
	// 
	//Struct normally packs the data as close as possible so it 
	//packs like this: (host memory layout)
	//{x,y,r,g,b}

	//a float using 32bits is four bytes
	//a vec2 with float would therefore be 8 bytes
	//in device memory, vec3 requires to be aligned by multiple of 16 bytes
	//meaning that the device aligns like this:
	//{x,y,_,_,r,g,b}

	//this means r and g from the memory will be assigned to blank memory, causing those value to be unread
	//to fix this, align the vec3 variable to the multiple of 16 bytes with alignas(16)


	namespace ConsoleCommand {
		//WireframeRenderSystem controllers here
		
	}

	inline void SendPushConstantData(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout, WireframePushConstantData& push)
	{
		vkCmdPushConstants(
			cmdBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(WireframePushConstantData),
			&push);
	}

	//Function that binds index buffer if the index count of the component is more than 0
	inline void BindVertexIndexBuffer(VkCommandBuffer cmdBuffer, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, uint32_t indexCount, VkBuffer buffer) {
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, pBuffers, pOffsets);
		if (indexCount > 0) vkCmdBindIndexBuffer(cmdBuffer, buffer, 0, VK_INDEX_TYPE_UINT32);
	}
	inline void DrawByIndexCount(VkCommandBuffer cmdBuffer, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex) {
		if (indexCount > 0) {
			vkCmdDrawIndexed(cmdBuffer, indexCount, instanceCount, firstIndex, 0, 0);
		}
		else {
			vkCmdDraw(cmdBuffer, vertexCount, instanceCount, firstIndex, 0);
		}
	}

	//Set up pipeline configuration here
	void WireframePipelineConfigModifier(PipelineConfigInfo& pipelineConfig) {
		pipelineConfig.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
		pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
		pipelineConfig.rasterizationInfo.lineWidth = 1.0f;
	}

	static void uploadBuffer(BGLDevice& dev, VkBufferUsageFlags usage,
	                         const void* src, VkDeviceSize size,
	                         VkBuffer& dstBuf, VkDeviceMemory& dstMem)
	{
		VkBuffer stagingBuf; VkDeviceMemory stagingMem;
		dev.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuf, stagingMem);
		void* mapped;
		vkMapMemory(BGLDevice::device(), stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped);
		memcpy(mapped, src, size);
		vkUnmapMemory(BGLDevice::device(), stagingMem);
		dev.createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dstBuf, dstMem);
		dev.copyBuffer(stagingBuf, dstBuf, size);
		vkDestroyBuffer(BGLDevice::device(), stagingBuf, nullptr);
		vkFreeMemory(BGLDevice::device(), stagingMem, nullptr);
	}

	WireframeRenderSystem::WireframeRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry,
		BGLDevice &bglDevice) :
		BGLRenderSystem{ renderPass, setLayouts, sizeof(WireframePushConstantData) },
		descriptorManager{ _descriptorManager },
		registry{ _registry },
		device{ bglDevice }
	{
		std::cout << "Creating Wireframe Render System\n";
		createPipeline(renderPass, "/shaders/wireframe_shader.vert.spv", "/shaders/wireframe_shader.frag.spv", WireframePipelineConfigModifier);

		// Build shared unit wire cube [-1,-1,-1]→[1,1,1] for bbox drawing
		auto corner = [](float x, float y, float z) {
			BGLModel::Vertex v{};
			v.position = {x, y, z};
			return v;
		};
		const std::array<BGLModel::Vertex, 8> verts = {
			corner(-1,-1,-1), corner( 1,-1,-1),
			corner( 1, 1,-1), corner(-1, 1,-1),
			corner(-1,-1, 1), corner( 1,-1, 1),
			corner( 1, 1, 1), corner(-1, 1, 1),
		};
		// 12 edges: bottom face, top face, 4 verticals
		const std::array<uint32_t, BBOX_INDEX_COUNT> idx = {
			0,1, 1,2, 2,3, 3,0,
			4,5, 5,6, 6,7, 7,4,
			0,4, 1,5, 2,6, 3,7,
		};
		uploadBuffer(bglDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			verts.data(), sizeof(verts), bboxVertexBuffer, bboxVertexMemory);
		uploadBuffer(bglDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			idx.data(), sizeof(idx), bboxIndexBuffer, bboxIndexMemory);
	}

	WireframeRenderSystem::~WireframeRenderSystem()
	{
		if (bboxVertexBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(BGLDevice::device(), bboxVertexBuffer, nullptr);
			vkFreeMemory(BGLDevice::device(), bboxVertexMemory, nullptr);
		}
		if (bboxIndexBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(BGLDevice::device(), bboxIndexBuffer, nullptr);
			vkFreeMemory(BGLDevice::device(), bboxIndexMemory, nullptr);
		}
	}

	void WireframeRenderSystem::renderEntities(FrameInfo& frameInfo)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, //first set
			1, //descriptorSet Count 
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize offsets[] = { 0 };
		auto transformCompView = registry.group<>(entt::get<TransformComponent, WireframeComponent>);
		for (auto [entity, transformComp, modelDescComp] : transformCompView.each()) {
			BindVertexIndexBuffer(frameInfo.commandBuffer, &modelDescComp.vertexBuffer, offsets, modelDescComp.indexCount, modelDescComp.indexBuffer);

			WireframePushConstantData push{};
			push.UsesBufferedTransform = 0;
			push.modelMatrix = transformComp.mat4();
			push.scale = glm::vec4{ transformComp.getWorldScale(), 1.0 };
			for (uint32_t i = 0; i < modelDescComp.submeshCount; i++) {
				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
				DrawByIndexCount(frameInfo.commandBuffer, modelDescComp.vertexCount, modelDescComp.indexCount, 1, modelDescComp.submeshes[i].firstIndex);
			}
		}
		
		auto instancedRenderView = registry.group<>(entt::get<TransformArrayComponent, WireframeComponent>);
		for (auto [entity, transformComp, modelDescComp] : instancedRenderView.each()) {
			BindVertexIndexBuffer(frameInfo.commandBuffer, &modelDescComp.vertexBuffer, offsets, modelDescComp.indexCount, modelDescComp.indexBuffer);

			WireframePushConstantData push{};
			push.UsesBufferedTransform   = transformComp.useBuffer() ? 1 : 0;
			push.BufferedTransformHandle = transformComp.bufferHandle;
			if (!transformComp.useBuffer()) {
				push.modelMatrix = transformComp.mat4(0);
				push.scale = glm::vec4{ transformComp.getWorldScale(0), 1.0 };
			}
			for (uint32_t i = 0; i < modelDescComp.submeshCount; i++) {
				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
				DrawByIndexCount(frameInfo.commandBuffer, modelDescComp.vertexCount, modelDescComp.indexCount, transformComp.count(), modelDescComp.submeshes[i].firstIndex);
			}
		}

		if (!drawCollision) return;
		//Start drawing collision models
		//Update UBO before drawing collision models.
		//Collision models will all be yellow by default

		auto collisionModelView = registry.view<TransformComponent, CollisionModelComponent>();
		for (auto [entity, transformComp, collisionModelComp] : collisionModelView.each()) {
			BindVertexIndexBuffer(frameInfo.commandBuffer, &collisionModelComp.vertexBuffer, offsets, collisionModelComp.indexCount, collisionModelComp.indexBuffer);

			WireframePushConstantData push{};
			push.UsesBufferedTransform = 0;
			push.modelMatrix = transformComp.mat4();
			push.scale = glm::vec4{ collisionModelComp.collisionScale, 1.0 };
			for (uint32_t i = 0; i < collisionModelComp.submeshCount; i++) {
				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
				DrawByIndexCount(frameInfo.commandBuffer, collisionModelComp.vertexCount, collisionModelComp.indexCount, 1, collisionModelComp.submeshes[i].firstIndex);
			}
		}
	}

	void WireframeRenderSystem::renderBBoxes(FrameInfo& frameInfo)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &bboxVertexBuffer, &zero);
		vkCmdBindIndexBuffer(frameInfo.commandBuffer, bboxIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		auto view = registry.view<TransformComponent, ModelComponent>();
		for (auto [entity, transformComp, modelComp] : view.each()) {
			if (modelComp.aabbMin == modelComp.aabbMax) continue;

			glm::vec3 center  = (modelComp.aabbMin + modelComp.aabbMax) * 0.5f;
			glm::vec3 halfExt = (modelComp.aabbMax - modelComp.aabbMin) * 0.5f;

			glm::mat4 bboxMat = transformComp.mat4()
				* glm::translate(glm::mat4{1.0f}, center)
				* glm::scale(glm::mat4{1.0f}, halfExt);

			WireframePushConstantData push{};
			push.UsesBufferedTransform = 0;
			push.modelMatrix = bboxMat;
			push.scale = glm::vec4{1.0f};
			push.color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
			SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
			vkCmdDrawIndexed(frameInfo.commandBuffer, BBOX_INDEX_COUNT, 1, 0, 0, 0);
		}
	}
}

