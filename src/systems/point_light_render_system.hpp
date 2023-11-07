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
	class PointLightSystem {
	public:

		PointLightSystem(BGLDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
		~PointLightSystem();

		PointLightSystem(const PointLightSystem&) = delete;
		PointLightSystem& operator=(const PointLightSystem&) = delete;

		void update(FrameInfo& frameInfo, GlobalUBO& ubo);
		void render(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
		void createPipeline(VkRenderPass renderPass);

		BGLDevice& bglDevice;

		std::unique_ptr<BGLPipeline> bglPipeline;
		VkPipelineLayout pipelineLayout;

	};

}