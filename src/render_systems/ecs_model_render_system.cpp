#include "ecs_model_render_system.hpp"
#include "../bagel_ecs_components.hpp"

#include "../bagel_engine_device.hpp"
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

	struct SimplePushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		// alignas aligns the variable to the 
		glm::mat4 normalMatrix{ 1.0f };
	};

	struct ECSPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::mat4 normalMatrix{ 1.0f };

		uint32_t diffuseTextureHandle;
		uint32_t emissionTextureHandle;
		uint32_t normalTextureHandle;
		uint32_t roughmetalTextureHandle;
		// Stores flags to determine which textures are present
		uint32_t textureMapFlag;

		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform = 0;
	};

	struct OBJDataBufferUnit {
		glm::mat4 modelMatrix{ 1.0f };
		glm::mat4 normalMatrix{ 1.0f };
	};

	void sendPushConstantData(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout, ECSPushConstantData& push)
	{
		vkCmdPushConstants(
			cmdBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(ECSPushConstantData),
			&push);
	}

	ModelRenderSystem::ModelRenderSystem(BGLDevice& device, VkRenderPass renderPass, std::vector<VkDescriptorSetLayout> setLayouts, std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager) : bglDevice{ device }, descriptorManager{ _descriptorManager }
	{
		createPipelineLayout(setLayouts);
		createPipeline(renderPass);
	}
	ModelRenderSystem::~ModelRenderSystem()
	{
		vkDestroyPipelineLayout(BGLDevice::device(), pipelineLayout, nullptr);
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

		auto transformCompView = frameInfo.registry.view<TransformComponent, ModelDescriptionComponent>();
		for (auto [entity, transformComp, modelDescComp] : transformCompView.each()) {

			VkBuffer buffers[] = { modelDescComp.vertexBuffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, buffers, offsets);

			ECSPushConstantData push{};

			if (modelDescComp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::DIFFUSE)	push.diffuseTextureHandle =		frameInfo.registry.get<DiffuseTextureComponent>(entity).textureHandle;
			if (modelDescComp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::EMISSION)	push.emissionTextureHandle =	frameInfo.registry.get<EmissionTextureComponent>(entity).textureHandle;
			if (modelDescComp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::NORMAL)		push.normalTextureHandle =		frameInfo.registry.get<NormalTextureComponent>(entity).textureHandle;
			if (modelDescComp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::ROUGHMETAL)	push.roughmetalTextureHandle =	frameInfo.registry.get<RoughnessMetalTextureComponent>(entity).textureHandle;
			
			push.textureMapFlag = modelDescComp.textureMapFlag;

			push.UsesBufferedTransform = 0;
			push.modelMatrix = transformComp.mat4();
			push.normalMatrix = transformComp.normalMatrix();

			sendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);

			if (modelDescComp.hasIndexBuffer) {
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, modelDescComp.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(frameInfo.commandBuffer, modelDescComp.indexCount, 1, 0, 0, 0);
			} else {
				vkCmdDraw(frameInfo.commandBuffer, modelDescComp.vertexCount, 1, 0, 0);
			}
		}

		auto instancedRenderView = frameInfo.registry.view<TransformArrayComponent, ModelDescriptionComponent>();
		for (auto [entity, transformComp, modelDescComp] : instancedRenderView.each()) {

			VkBuffer buffers[] = { modelDescComp.vertexBuffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, buffers, offsets);

			ECSPushConstantData push{};
			if (modelDescComp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::DIFFUSE) {
				push.diffuseTextureHandle = frameInfo.registry.get<DiffuseTextureComponent>(entity).textureHandle;
			}
			if (modelDescComp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::EMISSION) {
				push.emissionTextureHandle = frameInfo.registry.get<EmissionTextureComponent>(entity).textureHandle;
			}
			if (modelDescComp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::NORMAL) {
				push.normalTextureHandle = frameInfo.registry.get<NormalTextureComponent>(entity).textureHandle;
			}
			if (modelDescComp.textureMapFlag & ModelDescriptionComponent::TextureCompositeFlag::ROUGHMETAL) {
				push.roughmetalTextureHandle = frameInfo.registry.get<RoughnessMetalTextureComponent>(entity).textureHandle;
			}

			push.textureMapFlag = modelDescComp.textureMapFlag;

			push.UsesBufferedTransform = transformComp.useBuffer() ? 1 : 0;
			push.BufferedTransformHandle = transformComp.bufferHandle;

			if (!transformComp.useBuffer()) {
				push.modelMatrix = transformComp.mat4(0);
				push.normalMatrix = transformComp.normalMatrix(0);
			}

			sendPushConstantData(frameInfo.commandBuffer, pipelineLayout,push);

			if (modelDescComp.hasIndexBuffer) {
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, modelDescComp.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(frameInfo.commandBuffer, modelDescComp.indexCount, transformComp.maxIndex, 0, 0, 0);
			}
			else {
				vkCmdDraw(frameInfo.commandBuffer, modelDescComp.vertexCount, transformComp.maxIndex, 0, 0);
			}
		}
	}

	void ModelRenderSystem::createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts)
	{
		VkPushConstantRange pushConstantRange{};
		// This flag indicates that we want this pushconstantdata to be accessible in both vertex and fragment shaders
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		// offset mainly for if you want to use separate ranges for vertex and fragment shader
		pushConstantRange.offset = 0;

		pushConstantRange.size = sizeof(ECSPushConstantData);

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

		//desciptor set layout information
		std::cout << "Creating Model Render System Pipeline with descriptorSetLayout count of " << setLayouts.size() << "\n";
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();

		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(BGLDevice::device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create pipeline layout");
		}
	}
	void ModelRenderSystem::createPipeline(VkRenderPass renderPass)
	{
		//assert(bglSwapChain != nullptr && "Cannot create pipeline before swapchain");
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		PipelineConfigInfo pipelineConfig{};
		BGLPipeline::defaultPipelineConfigInfo(pipelineConfig);
		pipelineConfig.renderPass = renderPass;
		pipelineConfig.pipelineLayout = pipelineLayout;
#define USE_ABS_PATH
#ifndef USE_ABS_PATH
		bglPipeline = std::make_unique<BGLPipeline>(
			bglDevice,
			"../shaders/simple_shader.vert.spv",
			"../shaders/simple_shader.frag.spv",
			pipelineConfig);
#else
		bglPipeline = std::make_unique<BGLPipeline>(
			bglDevice,
			"C:/Users/locti/OneDrive/Documents/VisualStudioProjects/VulkanEngine/shaders/simple_shader.vert.spv",
			"C:/Users/locti/OneDrive/Documents/VisualStudioProjects/VulkanEngine/shaders/simple_shader.frag.spv",
			pipelineConfig);
#endif
	}
	
}

