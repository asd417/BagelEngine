#pragma once

#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>
#include "engine/bagel_pipeline.hpp"
#include "engine/bagel_descriptors.hpp"


namespace bagel {

	class BGLComputeSystem {
	protected:
		BGLComputeSystem(
			std::vector<VkDescriptorSetLayout> setLayouts,
			size_t pushConstantSize);
		~BGLComputeSystem();

		BGLComputeSystem(const BGLComputeSystem&) = delete;
		BGLComputeSystem& operator=(const BGLComputeSystem&) = delete;

		void createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts, size_t pushConstantSize);
		void createPipeline(const char *computeShaderFilePath, void (*PipelineConfigInfoModifier)(ComputePipelineConfigInfo &) = nullptr);

		std::unique_ptr<BGLComputePipeline> bglPipeline;
		VkPipelineLayout pipelineLayout;
	};

}