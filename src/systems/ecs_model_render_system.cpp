#include "ecs_model_render_system.hpp"
#include "../bagel_ecs_components.hpp"

#include <stdexcept>
#include <array>

#include <chrono>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#define BINDLESS

#define MAX_TRANSFORM_ARRAYSIZE 1000
#define BUFFER_COMP_TEST

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
		uint32_t textureHandle;
		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform = 0;
	};

	struct OBJDataBufferUnit {
		glm::mat4 modelMatrix{ 1.0f };
		glm::mat4 normalMatrix{ 1.0f };
	};

	ModelRenderSystem::ModelRenderSystem(BGLDevice& device, VkRenderPass renderPass, std::vector<VkDescriptorSetLayout> setLayouts, std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager) : bglDevice{ device }, descriptorManager{ _descriptorManager }
	{
#ifndef BUFFER_COMP_TEST
		objDataBuffer = std::make_unique<BGLBuffer>(
			bglDevice,
			sizeof(OBJDataBufferUnit),
			MAX_TRANSFORM_ARRAYSIZE,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		objDataBuffer->map();
		descriptorManager->storeBuffer(objDataBuffer->descriptorInfo());
#endif
		createPipelineLayout(setLayouts);
		createPipeline(renderPass);
	}
	ModelRenderSystem::~ModelRenderSystem()
	{
		vkDestroyPipelineLayout(bglDevice.device(), pipelineLayout, nullptr);
#ifndef BUFFER_COMP_TEST
		objDataBuffer->unmap();
#endif
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

		auto view = frameInfo.registry.view<TransformComponent, ModelDescriptionComponent, TextureComponent>();
		for (auto [entity, transformComp, modelDescComp, textureComp] : view.each()) {

			VkBuffer buffers[] = { modelDescComp.vertexBuffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, buffers, offsets);

			ECSPushConstantData push{};
			push.textureHandle = textureComp.textureHandle;
			push.UsesBufferedTransform = transformComp.useBuffer() ? 1 : 0;
			push.BufferedTransformHandle = transformComp.bufferHandle;
			//std::cout << "push.useInstancedTransform is " << push.useInstancedTransform << "\n";
			if (!transformComp.useBuffer()) {
				push.modelMatrix = transformComp.mat4(0);
				push.normalMatrix = transformComp.normalMatrix(0);
			}

			vkCmdPushConstants(
				frameInfo.commandBuffer,
				pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				sizeof(ECSPushConstantData),
				&push);

#ifndef BUFFER_COMP_TEST
			for (int i = 0; i < transformComp.translation.size(); i++) {
				OBJDataBufferUnit objData{};
				objData.modelMatrix = transformComp.mat4(i);
				objData.normalMatrix = transformComp.normalMatrix(i);

				objDataBuffer->writeToBuffer(&objData, sizeof(objData), i * sizeof(OBJDataBufferUnit));
			}
			objDataBuffer->flush();
#endif
			
			if (modelDescComp.hasIndexBuffer) {
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, modelDescComp.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(frameInfo.commandBuffer, modelDescComp.indexCount, transformComp.translation.size(), 0, 0, 0);
			} else {
				vkCmdDraw(frameInfo.commandBuffer, modelDescComp.vertexCount, transformComp.translation.size(), 0, 0);
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

		if (vkCreatePipelineLayout(bglDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
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

