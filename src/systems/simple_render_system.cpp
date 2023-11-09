#include "simple_render_system.hpp"

#include <stdexcept>
#include <array>

#include <chrono>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
//#define SIERPINSKITRI_EXAMPLE
//#define SIERPINSKITRI_USESOL2
//#define SIERPINSKITRI_LOOPSOLUTION

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
	SimpleRenderSystem::SimpleRenderSystem(BGLDevice& device, VkRenderPass renderPass, std::vector<VkDescriptorSetLayout> setLayouts) : bglDevice{device}
	{
		createPipelineLayout(setLayouts);
		createPipeline(renderPass);
	}
	SimpleRenderSystem::~SimpleRenderSystem()
	{
		vkDestroyPipelineLayout(bglDevice.device(), pipelineLayout, nullptr);
	}
	
	void SimpleRenderSystem::renderGameObjects(FrameInfo& frameInfo)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		//uint32_t ints[1] = {static_cast<uint32_t>(sizeof(frameInfo.globalDescriptorSets[1])) };
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, //first set
			1, //descriptorSet Count 
			&frameInfo.globalDescriptorSets,
			0, nullptr);
		
		for (auto& kv : frameInfo.gameObjects) {
			auto& obj = kv.second;
			if (obj.model == nullptr) continue;

			auto objTextureDescriptor = obj.model->getTextureDescriptorSet();
			std::array<VkDescriptorSet, 1> descriptors = { objTextureDescriptor };
			obj.model->bind(frameInfo.commandBuffer);
			vkCmdBindDescriptorSets(
				frameInfo.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout,
				1, //first set
				descriptors.size(), //descriptorSet Count 
				descriptors.data(),
				0, nullptr);
			for (int i = 0; i < obj.transforms.size(); i++) {
				SimplePushConstantData push{};
				push.modelMatrix = obj.transforms[i].mat4();
				push.normalMatrix = obj.transforms[i].normalMatrix();
				vkCmdPushConstants(
					frameInfo.commandBuffer,
					pipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0,
					sizeof(SimplePushConstantData),
					&push);
				obj.model->draw(frameInfo.commandBuffer);
			}
		}
	}

	void SimpleRenderSystem::createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts)
	{
		VkPushConstantRange pushConstantRange{};
		// This flag indicates that we want this pushconstantdata to be accessible in both vertex and fragment shaders
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		// offset mainly for if you want to use separate ranges for vertex and fragment shader
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(SimplePushConstantData);


		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

		//desciptor set layout information
		std::cout << "Creating Simple Render System Pipeline with descriptorSetLayout count of " << setLayouts.size() << "\n";
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();

		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(bglDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create pipeline layout");
		}
	}
	void SimpleRenderSystem::createPipeline(VkRenderPass renderPass)
	{
		//assert(bglSwapChain != nullptr && "Cannot create pipeline before swapchain");
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		PipelineConfigInfo pipelineConfig{};
		BGLPipeline::defaultPipelineConfigInfo(pipelineConfig);
		pipelineConfig.renderPass = renderPass;
		pipelineConfig.pipelineLayout = pipelineLayout;
		bglPipeline = std::make_unique<BGLPipeline>(
			bglDevice,
			"../shaders/simple_shader.vert.spv",
			"../shaders/simple_shader.frag.spv",
			pipelineConfig);
	}
}

