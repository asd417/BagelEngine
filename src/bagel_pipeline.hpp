#pragma once

#include <string>
#include <vector>
#include "bagel_engine_device.hpp"

namespace bagel {

	struct PipelineConfigInfo {
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
		// If non-empty, overrides colorBlendAttachment for multi-attachment pipelines (e.g. G-buffer)
		std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments{};
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
		BGLPipeline(std::string vertFilePath, std::string fragFilePath, const PipelineConfigInfo& configInfo);
		~BGLPipeline();

		// Delete these copy constructors to avoid duplicating the pointers to the vulkan objects
		BGLPipeline(const BGLPipeline&) = delete;
		BGLPipeline operator=(const BGLPipeline&) = delete;
		void bind(VkCommandBuffer commandBuffer);

		static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);
		static void enableAlphaBlending(PipelineConfigInfo& configInfo);
		// 3-attachment pipeline for G-buffer fill pass (position, normal, albedo)
		static void setupGBufferPipeline(PipelineConfigInfo& configInfo);
		// No-vertex-input, depth-disabled pipeline for full-screen composition pass
		static void setupFullScreenPipeline(PipelineConfigInfo& configInfo);
		// Alpha-blended forward pipeline for transparent objects: depth test ON, depth write OFF
		static void setupTransparentPipeline(PipelineConfigInfo& configInfo);
		// Full-screen pipeline with additive blending (src=ONE, dst=ONE) for bloom upsample accumulation
		static void setupFullScreenAdditivePipeline(PipelineConfigInfo& configInfo);

		VkSubpassDescription createSubpassDescription(int colorAttachmentCount, const VkAttachmentReference* colorAttachmentReferences, const VkAttachmentReference* depthAttachmentReferences) {
			VkSubpassDescription sd;
			sd.colorAttachmentCount = colorAttachmentCount;
			sd.pColorAttachments = colorAttachmentReferences;
			sd.pDepthStencilAttachment = depthAttachmentReferences;
			sd.pipelineBindPoint = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;
			return sd;
		}
		
	private:
		static std::vector<char> readFile(const std::string& filepath);

		void createGraphicsPipeline(const std::string& vertFilePath, const std::string& fragFilePath, const PipelineConfigInfo &configInfo);


		//shaderModule is a pointer to a pointer
		void createShaderModule(const std::vector<char>& code, VkShaderModule * shaderModule);

		//typedef pointer 
		VkPipeline graphicsPipeline;
		VkPipeline offscreenPipeline;

		// VkShaderModule is a pointer to VkShaderModule_T
		VkShaderModule vertShaderModule;
		VkShaderModule fragShaderModule;
	};
	
}