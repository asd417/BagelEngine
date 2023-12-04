#include "point_light_render_system.hpp"
#include "bagel_ecs_components.hpp"

#include <stdexcept>
#include <array>
#include <chrono>
#include <iostream>
#include <map>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {


	// PushConstantData is a performant and simple way to send data to vertex and fragment shader
	// It is typically faster than descriptor sets for frequently updated data
	// 
	// But its size is limited to 128 bytes (technically each device has different max size but only 128 is guaranteed)
	// This maximum size varies from device to device and is specified in bytes by the max_push_constants_size field of vk::PhysicalDeviceLimits
	// Even the high end device (RTX3080) has only 256 bytes available so it is unrealistic to send most data
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

	struct PointLightPushConstant {
		glm::vec4 positions{};
		glm::vec4 color{};
		float radius;
	};

	PointLightSystem::PointLightSystem(BGLDevice& device, VkRenderPass renderPass, std::vector<VkDescriptorSetLayout> globalSetLayouts) : bglDevice{device}
	{
		createPipelineLayout(globalSetLayouts);
		createPipeline(renderPass);
	}
	PointLightSystem::~PointLightSystem()
	{
		vkDestroyPipelineLayout(bglDevice.device(), pipelineLayout, nullptr);
	}
	
	void PointLightSystem::update(FrameInfo& frameInfo, GlobalUBO& ubo)
	{
		// Copy all point light information into the globalubo point light information
		int lightIndex = 0;
		auto view = frameInfo.registry.view<TransformComponent, PointLightComponent>();
		for (auto [entity, transformComp, pointLightComp] : view.each()) {
			auto rotateLight = glm::rotate(glm::mat4(1.0f), frameInfo.frameTime,{ 0.f,-1.f,0.f }); //axis of rotation
			transformComp.translation[0] = rotateLight * (glm::vec4(transformComp.translation[0], 1.f) - glm::vec4(0.f, 0.f, 5.0f, 1.0f)) + glm::vec4(0.f, 0.f, 5.0f, 1.0f);
			
			ubo.pointLights[lightIndex].position = glm::vec4(transformComp.translation[0],1.f);
			ubo.pointLights[lightIndex].color = glm::vec4(pointLightComp.color);
			lightIndex++;
		}
		ubo.numLights = lightIndex;
	}

	void PointLightSystem::render(FrameInfo& frameInfo)
	{
		//alpha sorting
		//transparent entities need to be drawn from back to front.
		glm::vec3 camPos = frameInfo.camera.getPosition();
		frameInfo.registry.sort<TransformComponent>([&](const auto& lhs, const auto& rhs) {
			glm::vec3 lhsOffset = camPos - lhs.translation[0];
			float lhsDisSquared = glm::dot(lhsOffset, lhsOffset);
			glm::vec3 rhsOffset = camPos - rhs.translation[0];
			float rhsDisSquared = glm::dot(rhsOffset, rhsOffset);

			//Need to sort it so that close things are last
			return lhsDisSquared > rhsDisSquared;
			});

		bglPipeline->bind(frameInfo.commandBuffer);

		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, //first set
			1, //descriptorSet Count 
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		// Create pushconstant and send it to device and draw.
		// Accessing in order of TransformComponent which was sorted above.
		auto view = frameInfo.registry.view<TransformComponent, PointLightComponent>();
		view.use<TransformComponent>();
		for (auto [entity, transformComp, pointLightComp] : view.each()) {
			PointLightPushConstant push{};
			push.positions = glm::vec4(transformComp.translation[0],1.f);
			push.color = pointLightComp.color;
			push.radius = pointLightComp.radius;
		
			vkCmdPushConstants(
				frameInfo.commandBuffer,
				pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, //offset of pushcontants
				sizeof(PointLightPushConstant),
				&push
			);
			vkCmdDraw(frameInfo.commandBuffer, 6, 1, 0, 0);
		}
	}

	void PointLightSystem::createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts)
	{
		VkPushConstantRange pushConstantRange{};
		// This flag indicates that we want this pushconstantdata to be accessible in both vertex and fragment shaders
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		// offset mainly for if you want to use separate ranges for vertex and fragment shader
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PointLightPushConstant);

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();

		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(bglDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create pipeline layout");
		}
	}
	void PointLightSystem::createPipeline(VkRenderPass renderPass)
	{
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		PipelineConfigInfo pipelineConfig{};
		BGLPipeline::defaultPipelineConfigInfo(pipelineConfig);
		BGLPipeline::enableAlphaBlending(pipelineConfig);
		pipelineConfig.renderPass = renderPass;
		pipelineConfig.pipelineLayout = pipelineLayout;

#define USE_ABS_PATH
#ifndef USE_ABS_PATH
		bglPipeline = std::make_unique<BGLPipeline>(
			bglDevice,
			"../shaders/point_light.vert.spv",
			"../shaders/point_light.frag.spv",
			pipelineConfig);
#else
		bglPipeline = std::make_unique<BGLPipeline>(
			bglDevice,
			"C:/Users/locti/OneDrive/Documents/VisualStudioProjects/VulkanEngine/shaders/point_light.vert.spv",
			"C:/Users/locti/OneDrive/Documents/VisualStudioProjects/VulkanEngine/shaders/point_light.frag.spv",
			pipelineConfig);
#endif
	}
}

