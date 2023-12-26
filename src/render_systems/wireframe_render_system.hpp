#pragma once

#include <glm/gtc/constants.hpp>

#include "../bagel_engine_device.hpp"
#include "../bagel_pipeline.hpp"

#include "../bagel_frame_info.hpp"
#include "../bgl_camera.hpp"
#include "../bgl_gameobject.hpp"
#include "../bgl_model.hpp"


#include "entt.hpp"

#include <memory>
#include <vector>

namespace bagel {

	class WireframeRenderSystem {
	public:
		WireframeRenderSystem(BGLDevice& device, VkRenderPass renderPass, std::vector<VkDescriptorSetLayout> setLayouts, std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager);
		~WireframeRenderSystem();

		WireframeRenderSystem(const WireframeRenderSystem&) = delete;
		WireframeRenderSystem& operator=(const WireframeRenderSystem&) = delete;
		void renderEntities(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts);
		void createPipeline(VkRenderPass renderPass);

		BGLDevice& bglDevice;

		std::unique_ptr<BGLPipeline> bglPipeline;
		VkPipelineLayout pipelineLayout;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
		std::unique_ptr <BGLBuffer> objDataBuffer;
	};

}