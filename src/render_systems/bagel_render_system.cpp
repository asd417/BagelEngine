#include "bagel_render_system.hpp"
#include "../bagel_ecs_components.hpp"
#include "../bagel_engine_device.hpp"
#include "../bagel_util.hpp"

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <array>
#include <chrono>
#include <iostream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {
	BGLRenderSystem::BGLRenderSystem(
		VkRenderPass renderPass, 
		std::vector<VkDescriptorSetLayout> setLayouts, 
		size_t pushConstantSize)
	{
		createPipelineLayout(setLayouts, pushConstantSize);
	}
	BGLRenderSystem::~BGLRenderSystem()
	{
		vkDestroyPipelineLayout(BGLDevice::device(), pipelineLayout, nullptr);
	}
	
	//Requires pushconstant typename
	void BGLRenderSystem::createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts, size_t pushConstantSize)
	{
		VkPushConstantRange pushConstantRange{};
		// This flag indicates that we want this pushconstantdata to be accessible in both vertex and fragment shaders
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		// offset mainly for if you want to use separate ranges for vertex and fragment shader
		pushConstantRange.offset = 0;
		pushConstantRange.size = pushConstantSize;

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
	void BGLRenderSystem::createPipeline(VkRenderPass renderPass, const char* vertexShaderFilePath, const char* fragmentShaderFilePath, void (*PipelineConfigInfoModifier)(PipelineConfigInfo&) = nullptr)
	{
		//assert(bglSwapChain != nullptr && "Cannot create pipeline before swapchain");
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		PipelineConfigInfo pipelineConfig{};
		BGLPipeline::defaultPipelineConfigInfo(pipelineConfig);
		pipelineConfig.renderPass = renderPass;
		pipelineConfig.pipelineLayout = pipelineLayout;
		if(PipelineConfigInfoModifier != nullptr) PipelineConfigInfoModifier(pipelineConfig);

		bglPipeline = std::make_unique<BGLPipeline>(
			util::enginePath(vertexShaderFilePath),
			util::enginePath(fragmentShaderFilePath),
			pipelineConfig);
	}
}

