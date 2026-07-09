#pragma once

#include <string>
#include <vector>
#include "engine/bagel_engine_device.hpp"

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
		// Depth-only pipeline for shadow map rendering: no color output, depth bias enabled
		static void setupShadowMapPipeline(PipelineConfigInfo& configInfo);

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

		// VkShaderModule is a pointer to VkShaderModule_T
		VkShaderModule vertShaderModule;
		VkShaderModule fragShaderModule;
	};

	// Compute counterpart to PipelineConfigInfo. A compute pipeline has no fixed-function state, so
	// this only carries what actually varies: the caller-owned layout, the SPIR-V entry point, the
	// pipeline create flags, and optional specialization constants (the usual way to set the
	// shader's local_size_x/y/z or other tunables at pipeline-creation time). Fill it with
	// defaultComputePipelineConfigInfo, then set pipelineLayout (and specialization data if wanted).
	struct ComputePipelineConfigInfo {
		ComputePipelineConfigInfo() = default;
		ComputePipelineConfigInfo(const ComputePipelineConfigInfo&) = delete;
		ComputePipelineConfigInfo operator=(const ComputePipelineConfigInfo&) = delete;

		VkPipelineLayout pipelineLayout = nullptr;
		const char* entryPointName = "main";
		VkPipelineCreateFlags flags = 0;

		// Specialization constants. Leave both empty to use the values baked into the shader.
		// specializationData is the raw backing bytes the map entries index into.
		std::vector<VkSpecializationMapEntry> specializationMapEntries{};
		std::vector<uint8_t> specializationData{};
	};

	// Compute pipeline: a single .comp SPIR-V stage, no render pass / vertex input / fixed-function
	// state. The pipeline layout (descriptor set layouts + push constant ranges) is owned by the
	// caller, exactly like PipelineConfigInfo::pipelineLayout is for BGLPipeline, so it is NOT
	// destroyed here. Dispatch must happen OUTSIDE a render pass, and the caller is responsible for
	// the barriers around it (transition storage images to GENERAL, and add a COMPUTE_SHADER ->
	// consumer-stage barrier before the result is read).
	class BGLComputePipeline {
	public:
		BGLComputePipeline(const std::string& compFilePath, const ComputePipelineConfigInfo& configInfo);
		~BGLComputePipeline();

		// Fills configInfo with sensible defaults ("main" entry point, no flags, no specialization).
		// The caller still must set configInfo.pipelineLayout before constructing the pipeline.
		static void defaultComputePipelineConfigInfo(ComputePipelineConfigInfo& configInfo);

		BGLComputePipeline(const BGLComputePipeline&) = delete;
		BGLComputePipeline& operator=(const BGLComputePipeline&) = delete;

		void bind(VkCommandBuffer commandBuffer);
		// groupCount* are workgroup counts, i.e. ceil(problemSize / local_size_*) from the shader.
		void dispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

		VkPipeline pipeline() const { return computePipeline; }

	private:
		static std::vector<char> readFile(const std::string& filepath);
		void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);
		void createComputePipeline(const std::string& compFilePath, const ComputePipelineConfigInfo& configInfo);

		VkPipeline computePipeline;
		VkShaderModule compShaderModule;
	};

}