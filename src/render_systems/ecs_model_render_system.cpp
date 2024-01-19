#include "ecs_model_render_system.hpp"
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
		//ModelRenderSystem console command functions here

	}
	inline void SendPushConstantData(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout, ECSPushConstantData& push)
	{
		vkCmdPushConstants(
			cmdBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(ECSPushConstantData),
			&push);
	}

	inline void FillPushConstantData(ECSPushConstantData& push, ModelComponent::Submesh sm, const entt::registry& r, const entt::entity& ent) {
		if (sm.textureMapFlag & ModelComponent::TextureCompositeFlag::DIFFUSE)		push.diffuseTextureHandle =		sm.diffuseTextureHandle;
		if (sm.textureMapFlag & ModelComponent::TextureCompositeFlag::EMISSION)	push.emissionTextureHandle =	sm.emissionTextureHandle;
		if (sm.textureMapFlag & ModelComponent::TextureCompositeFlag::NORMAL)		push.normalTextureHandle =		sm.normalTextureHandle;
		if (sm.textureMapFlag & ModelComponent::TextureCompositeFlag::ROUGHMETAL)	push.roughmetalTextureHandle =	sm.roughmetalTextureHandle;
		push.textureMapFlag = sm.textureMapFlag;
	}

	ModelRenderSystem::ModelRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry) :
		BGLRenderSystem{ renderPass, setLayouts, sizeof(ECSPushConstantData) },
		descriptorManager{ _descriptorManager },
		registry{ _registry }
	{
		std::cout << "Creating Model Render System\n";
		createPipeline(renderPass, "/shaders/simple_shader.vert.spv", "/shaders/simple_shader.frag.spv", nullptr);
	}

	void ModelRenderSystem::renderEntities(FrameInfo& frameInfo)
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
		//auto transformCompView = registry.view<TransformComponent, ModelComponent>();
		//for (auto [entity, transformComp, modelDescComp] : transformCompView.each()) {
		auto transformCompGroup = registry.group<>(entt::get<TransformComponent, ModelComponent>);
		for(auto [entity, transformComp, modelDescComp] : transformCompGroup.each()) {
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &modelDescComp.vertexBuffer, offsets);
			if (modelDescComp.indexCount > 0) vkCmdBindIndexBuffer(frameInfo.commandBuffer, modelDescComp.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			//Send pushconstant per submesh
			for (const auto& sm : modelDescComp.submeshes) {
				ECSPushConstantData push{};
				FillPushConstantData(push, sm, registry, entity);

				push.UsesBufferedTransform = 0;
				push.modelMatrix = transformComp.mat4();
				push.scale = glm::vec4{ transformComp.getWorldScale(), 1.0 };
				push.roughmetalmultiplier = sm.roughmetalmultiplier;

				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);

				if (modelDescComp.indexCount > 0) {
					vkCmdDrawIndexed(frameInfo.commandBuffer, modelDescComp.indexCount, 1, sm.firstIndex, 0, 0);
				}
				else {
					vkCmdDraw(frameInfo.commandBuffer, modelDescComp.vertexCount, 1, sm.firstIndex, 0);
				}
			}
		}

		auto transformArrayCompGroup = registry.group<>(entt::get<TransformArrayComponent, ModelComponent>);
		for (auto [entity, transformComp, modelDescComp] : transformArrayCompGroup.each()) {

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &modelDescComp.vertexBuffer, offsets);
			if (modelDescComp.indexCount > 0) vkCmdBindIndexBuffer(frameInfo.commandBuffer, modelDescComp.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			//Send pushconstant per submesh
			for (auto sm : modelDescComp.submeshes) {
				ECSPushConstantData push{};
				FillPushConstantData(push, sm, registry, entity);

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
	}

}

