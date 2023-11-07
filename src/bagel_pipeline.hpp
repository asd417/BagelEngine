#pragma once

#include <string>
#include <vector>
#include "bagel_engine_device.hpp"

namespace bagel {

	struct PipelineConfigInfo {
		// When removing copy constructors, either implement or default the constructor 
		PipelineConfigInfo() = default;
		PipelineConfigInfo(const PipelineConfigInfo&) = delete;
		PipelineConfigInfo operator=(const PipelineConfigInfo&) = delete;
		
		std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

		VkPipelineViewportStateCreateInfo viewportInfo;
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
		VkPipelineRasterizationStateCreateInfo rasterizationInfo;
		VkPipelineMultisampleStateCreateInfo multisampleInfo;
		VkPipelineColorBlendAttachmentState colorBlendAttachment;
		VkPipelineColorBlendStateCreateInfo colorBlendInfo;
		VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
		std::vector<VkDynamicState> dynamicStateEnables;
		VkPipelineDynamicStateCreateInfo dynamicStateInfo;

		VkPipelineLayout pipelineLayout = nullptr;
		VkRenderPass renderPass = nullptr;
		uint32_t subpass = 0;
	};

	class BGLPipeline {
	public:
		BGLPipeline(BGLDevice& device, const std::string& vertFilePath, const std::string& fragFilePath, const PipelineConfigInfo& configInfo);

		~BGLPipeline();

		// Delete these copy constructors to avoid duplicating the pointers to the vulkan objects
		BGLPipeline(const BGLPipeline&) = delete;
		BGLPipeline operator=(const BGLPipeline&) = delete;
		void bind(VkCommandBuffer commandBuffer);

		static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);
		static void enableAlphaBlending(PipelineConfigInfo& configInfo);
		
	private:
		static std::vector<char> readFile(const std::string& filepath);

		void createGraphicsPipeline(const std::string& vertFilePath, const std::string& fragFilePath, const PipelineConfigInfo &configInfo);

		// A pipeline fundamentally needs a device. Therefore setting this memory unsafe variable is not necessarily dangerous
		// In ULM this type of relation is known as Agregation


		//shaderModule is a pointer to a pointer
		void createShaderModule(const std::vector<char>& code, VkShaderModule * shaderModule);

		BGLDevice& bglDevice;
		//typedef pointer 
		VkPipeline graphicsPipeline;
		// VkShaderModule is a pointer to VkShaderModule_T
		VkShaderModule vertShaderModule;
		VkShaderModule fragShaderModule;
	};
	
}