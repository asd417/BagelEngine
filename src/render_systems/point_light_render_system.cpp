#include "point_light_render_system.hpp"
#include "bagel_ecs_components.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include <stdexcept>
#include <array>
#include <chrono>
#include <iostream>
#include <map>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {
	// Update Position
	void PointLightSystem::update(GlobalUBO& ubo, float frameTime)
	{
		// Copy all point light information into the globalubo point light information
		int lightIndex = 0;
		auto group = registry.group<>(entt::get<TransformComponent, PointLightComponent>);
		for (auto [entity, transformComp, pointLightComp] : group.each()) {
			auto rotateLight = glm::rotate(glm::mat4(1.0f), frameTime,{ 0.f,-1.f,0.f }); //axis of rotation
			transformComp.setTranslation(rotateLight * (glm::vec4(transformComp.getTranslation(), 1.f)));
			
			ubo.pointLights[lightIndex].position = glm::vec4(transformComp.getTranslation(), 1.f);
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
		frameInfo.registry.sort<TransformComponent>([&](const TransformComponent& lhs, const TransformComponent& rhs) {
			glm::vec3 lhsOffset = camPos - lhs.getTranslation();
			float lhsDisSquared = glm::dot(lhsOffset, lhsOffset);
			glm::vec3 rhsOffset = camPos - rhs.getTranslation();
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
			push.positions = glm::vec4(transformComp.getTranslation(), 1.f);
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
#ifdef POINTLIGHT_ORIGINAL

	PointLightSystem::PointLightSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> globalSetLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry) : registry{ _registry }
	{
		createPipelineLayout(globalSetLayouts);
		createPipeline(renderPass);
	}
	PointLightSystem::~PointLightSystem()
	{
		vkDestroyPipelineLayout(BGLDevice::device(), pipelineLayout, nullptr);
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

		if (vkCreatePipelineLayout(BGLDevice::device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
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
			"../shaders/point_light.vert.spv",
			"../shaders/point_light.frag.spv",
			pipelineConfig);
#else
		bglPipeline = std::make_unique<BGLPipeline>(
			"C:/Users/locti/OneDrive/Documents/VisualStudioProjects/VulkanEngine/shaders/point_light.vert.spv",
			"C:/Users/locti/OneDrive/Documents/VisualStudioProjects/VulkanEngine/shaders/point_light.frag.spv",
			pipelineConfig);
#endif
	}
#endif
}

