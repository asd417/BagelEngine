#pragma once

#include <glm/gtc/constants.hpp>

#include "../bagel_engine_device.hpp"
#include "../bagel_pipeline.hpp"

#include "../bagel_frame_info.hpp"
#include "../bgl_camera.hpp"
#include "../bgl_gameobject.hpp"
#include "../bgl_model.hpp"

#include <memory>
#include <vector>

namespace bagel {
	class SimpleRenderSystem {
	public:

		SimpleRenderSystem(BGLDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
		~SimpleRenderSystem();

		SimpleRenderSystem(const SimpleRenderSystem&) = delete;
		SimpleRenderSystem& operator=(const SimpleRenderSystem&) = delete;
		void renderGameObjects(FrameInfo &frameInfo);

	private:
		void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
		void createPipeline(VkRenderPass renderPass);

		BGLDevice& bglDevice;

		std::unique_ptr<BGLPipeline> bglPipeline;
		VkPipelineLayout pipelineLayout;

	};

}