#pragma once

#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>


#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "../bagel_camera.hpp"
#include "../bagel_gameobject.hpp"
#include "../bagel_model.hpp"


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