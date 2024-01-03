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
		//ModelRenderSystem controllers
		char* StopBind(void* ptr)
		{
			ModelRenderSystem* mrs = static_cast<ModelRenderSystem*>(ptr);
			mrs->stopBinding = !mrs->stopBinding;
			if (mrs->stopBinding) return "Stopped binding descriptorSet";
			else return "Binding descriptorSet";
		}
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

	inline void FillPushConstantData(ECSPushConstantData& push, const ModelDescriptionComponent& comp, const entt::registry& r, const entt::entity& ent) {
		if (comp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::DIFFUSE)		push.diffuseTextureHandle =		r.get<DiffuseTextureComponent>(ent).textureHandle;
		if (comp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::EMISSION)	push.emissionTextureHandle =	r.get<EmissionTextureComponent>(ent).textureHandle;
		if (comp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::NORMAL)		push.normalTextureHandle =		r.get<NormalTextureComponent>(ent).textureHandle;
		if (comp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::ROUGHMETAL)	push.roughmetalTextureHandle =	r.get<RoughnessMetalTextureComponent>(ent).textureHandle;
		push.textureMapFlag = comp.textureMapFlag;
	}

	inline void BindAppropriateModelBuffer(
		VkCommandBuffer& commandBuffer, 
		const std::unique_ptr<BGLModelBufferManager>& modelBufferManager, 
		const BGLModelBufferManager::BufferHandlePair& handles, 
		ModelDescriptionComponent& comp, 
		uint32_t instanceCount)
	{
		if (comp.indexCount > 0) {
			const VkBuffer& ibuffer = modelBufferManager->GetIndexBufferHandle(handles);
			vkCmdBindIndexBuffer(commandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffer, comp.indexCount, instanceCount, 0, 0, 0);
		}
		else {
			vkCmdDraw(commandBuffer, comp.vertexCount, instanceCount, 0, 0);
		}
	}

	ModelRenderSystem::ModelRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		std::unique_ptr<BGLModelBufferManager> const& _modelBufferManager,
		entt::registry& _registry,
		ConsoleApp& consoleApp) :
		BGLRenderSystem{ renderPass, setLayouts, sizeof(ECSPushConstantData) },
		descriptorManager{ _descriptorManager },
		modelBufferManager{ _modelBufferManager },
		registry{ _registry }
	{
		std::cout << "Creating Model Render System\n";
		consoleApp.AddCommand("STOPBINDING", this, ConsoleCommand::StopBind);
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
		auto transformCompView = registry.view<TransformComponent, ModelDescriptionComponent>();
		for (auto [entity, transformComp, modelDescComp] : transformCompView.each()) {
			//VkBuffer buffers[] = { modelDescComp.vertexBuffer };
			const BGLModelBufferManager::BufferHandlePair& handles = modelBufferManager->GetModelHandle(modelDescComp.modelName);
			const VkBuffer& vbuffer = modelBufferManager->GetVertexBufferHandle(handles);
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &vbuffer, offsets);

			ECSPushConstantData push{};
			FillPushConstantData(push, modelDescComp,registry,entity);

			push.UsesBufferedTransform = 0;
			push.modelMatrix = transformComp.mat4();
			push.normalMatrix = transformComp.normalMatrix();

			SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
			BindAppropriateModelBuffer(frameInfo.commandBuffer, modelBufferManager, handles, modelDescComp, 1);
		}

		auto instancedRenderView = registry.view<TransformArrayComponent, ModelDescriptionComponent>();
		for (auto [entity, transformComp, modelDescComp] : instancedRenderView.each()) {
			const BGLModelBufferManager::BufferHandlePair& handles = modelBufferManager->GetModelHandle(modelDescComp.modelName);
			const VkBuffer& vbuffer = modelBufferManager->GetVertexBufferHandle(handles);
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &vbuffer, offsets);

			ECSPushConstantData push{};
			FillPushConstantData(push, modelDescComp, registry, entity);

			push.UsesBufferedTransform = transformComp.useBuffer() ? 1 : 0;
			push.BufferedTransformHandle = transformComp.bufferHandle;

			if (!transformComp.useBuffer()) {
				push.modelMatrix = transformComp.mat4(0);
				push.normalMatrix = transformComp.normalMatrix(0);
			}

			SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
			BindAppropriateModelBuffer(frameInfo.commandBuffer, modelBufferManager, handles, modelDescComp, 1);
		}
	}

}

