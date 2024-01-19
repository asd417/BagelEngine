#include "wireframe_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"
#include "../bagel_util.hpp"
//#include "../bagel_console_commands.hpp"

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <array>
#include <chrono>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

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

	void pipelineConfigModifier(PipelineConfigInfo& pipelineConfig) {
		pipelineConfig.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
		pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
		pipelineConfig.rasterizationInfo.lineWidth = 1.0f;
	}

	WireframeRenderSystem::WireframeRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry) :
		BGLRenderSystem{ renderPass, setLayouts, sizeof(WireframePushConstantData) },
		descriptorManager{ _descriptorManager },
		registry{ _registry }
	{
		//Add console commands here
		//consoleApp.AddCommand("STOPBINDING", this, ConsoleCommand::StopBind);
		std::cout << "Creating Wireframe Render System\n";
		createPipeline(renderPass, "/shaders/wireframe_shader.vert.spv", "/shaders/wireframe_shader.frag.spv", pipelineConfigModifier);
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
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &modelDescComp.vertexBuffer, offsets);
			if (modelDescComp.indexCount > 0) vkCmdBindIndexBuffer(frameInfo.commandBuffer, modelDescComp.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			//Send pushconstant per submesh
			for (auto sm : modelDescComp.submeshes) {
				WireframePushConstantData push{};
				
				push.UsesBufferedTransform = 0;
				push.modelMatrix = transformComp.mat4();
				push.scale = glm::vec4{ transformComp.getWorldScale(), 1.0 };

				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);

				if (modelDescComp.indexCount > 0) {
					vkCmdDrawIndexed(frameInfo.commandBuffer, modelDescComp.indexCount, 1, sm.firstIndex, 0, 0);
				}
				else {
					vkCmdDraw(frameInfo.commandBuffer, modelDescComp.vertexCount, 1, sm.firstIndex, 0);
				}
			}
		}
		auto instancedRenderView = registry.group<>(entt::get<TransformArrayComponent, WireframeComponent>);
		for (auto [entity, transformComp, modelDescComp] : instancedRenderView.each()) {

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &modelDescComp.vertexBuffer, offsets);
			if (modelDescComp.indexCount > 0) vkCmdBindIndexBuffer(frameInfo.commandBuffer, modelDescComp.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			//Send pushconstant per submesh
			for (const auto& sm : modelDescComp.submeshes) {
				WireframePushConstantData push{};

				push.UsesBufferedTransform = transformComp.useBuffer() ? 1 : 0;
				push.BufferedTransformHandle = transformComp.bufferHandle;

				if (!transformComp.useBuffer()) {
					push.modelMatrix = transformComp.mat4(0);
					push.scale = glm::vec4{ transformComp.getWorldScale(0), 1.0 };
				}

				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);

				if (modelDescComp.indexCount > 0) {
					vkCmdDrawIndexed(frameInfo.commandBuffer, modelDescComp.indexCount, transformComp.count(), sm.firstIndex, 0, 0);
				}
				else {
					vkCmdDraw(frameInfo.commandBuffer, modelDescComp.vertexCount, transformComp.count(), sm.firstIndex, 0);
				}
			}
		}
		if (!drawCollision) return;
		auto collisionModelView = registry.view<TransformComponent, CollisionModelComponent>();
		for (auto [entity, transformComp, collisionModelComp] : collisionModelView.each()) {
			//VkBuffer buffers[] = { modelDescComp.vertexBuffer };
			const VkBuffer& vbuffer = collisionModelComp.vertexBuffer;
			const VkBuffer& ibuffer = collisionModelComp.indexBuffer;
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &vbuffer, offsets);
			if (collisionModelComp.indexCount > 0) vkCmdBindIndexBuffer(frameInfo.commandBuffer, collisionModelComp.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			//Send pushconstant per submesh
			for (auto sm : collisionModelComp.submeshes) {
				WireframePushConstantData push{};

				push.UsesBufferedTransform = 0;
				push.modelMatrix = transformComp.mat4();
				push.scale = glm::vec4{ collisionModelComp.collisionScale, 1.0 };

				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);

				if (collisionModelComp.indexCount > 0) {
					vkCmdDrawIndexed(frameInfo.commandBuffer, collisionModelComp.indexCount, 1, sm.firstIndex, 0, 0);
				}
				else {
					vkCmdDraw(frameInfo.commandBuffer, collisionModelComp.vertexCount, 1, sm.firstIndex, 0);
				}
			}
		}
	}
}

