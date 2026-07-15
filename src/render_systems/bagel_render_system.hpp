#pragma once

#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>
#include "engine/bagel_pipeline.hpp"

namespace bagel {

	class BGLRenderSystem {
	protected:
		BGLRenderSystem(
			VkRenderPass renderPass, 
			std::vector<VkDescriptorSetLayout> setLayouts, 
			size_t pushConstantSize);
		~BGLRenderSystem();

		BGLRenderSystem(const BGLRenderSystem&) = delete;
		BGLRenderSystem& operator=(const BGLRenderSystem&) = delete;

		void createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts, size_t pushConstantSize);
		void createPipeline(VkRenderPass renderPass, const char* vertexShaderFilePath, const char* FragmentShaderFilePath, void (*PipelineConfigInfoModifier)(PipelineConfigInfo&) );

		std::unique_ptr<BGLPipeline> bglPipeline;
		VkPipelineLayout pipelineLayout;
	};

}