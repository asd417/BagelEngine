#include "compute_systems/bagel_compute_system.hpp"

#include <iostream>
#include <stdexcept>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "bagel_util.hpp"
#include "ecs/bagel_ecs_components.hpp"

#include "engine/bagel_engine_device.hpp"

namespace bagel {
	BGLComputeSystem::BGLComputeSystem(
		std::vector<VkDescriptorSetLayout> setLayouts,
		size_t pushConstantSize)
	{
		createPipelineLayout(setLayouts, pushConstantSize);
	}
	BGLComputeSystem::~BGLComputeSystem()
	{
		vkDestroyPipelineLayout(BGLDevice::device(), pipelineLayout, nullptr);
	}
	
	//Requires pushconstant typename
	void BGLComputeSystem::createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts, size_t pushConstantSize)
	{
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = static_cast<uint32_t>(pushConstantSize);

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		if (setLayouts.size() != 1) std::cout << "Creating compute pipeline with descriptorSetLayout count of " << setLayouts.size() << "\n";
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts    = setLayouts.data();

		if (pushConstantSize > 0) {
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges    = &pushConstantRange;
		} else {
			pipelineLayoutInfo.pushConstantRangeCount = 0;
			pipelineLayoutInfo.pPushConstantRanges    = nullptr;
		}

		if (vkCreatePipelineLayout(BGLDevice::device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create pipeline layout");
		}
	}
	void BGLComputeSystem::createPipeline(const char* computeShaderFilePath, void (*PipelineConfigInfoModifier)(ComputePipelineConfigInfo&))
	{
		//can this actually fire since the pipeline layout is in the initializer?
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		ComputePipelineConfigInfo pipelineConfig{};
		BGLComputePipeline::defaultComputePipelineConfigInfo(pipelineConfig);
		pipelineConfig.pipelineLayout = pipelineLayout;
		if(PipelineConfigInfoModifier != nullptr) PipelineConfigInfoModifier(pipelineConfig);

		bglPipeline = std::make_unique<BGLComputePipeline>(
			util::enginePath(computeShaderFilePath),
			pipelineConfig);
	}
}

