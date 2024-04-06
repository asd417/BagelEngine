#pragma once

#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_engine_device.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "../bgl_camera.hpp"
#include "../bgl_gameobject.hpp"
#include "../bgl_model.hpp"

#include <memory>
#include <vector>
//#define POINTLIGHT_ORIGINAL
namespace bagel {

	struct PointLightPushConstant {
		glm::vec4 positions{};
		glm::vec4 color{};
		float radius;
	};

#ifdef POINTLIGHT_ORIGINAL
	class PointLightSystem {
	public:
		PointLightSystem(
			VkRenderPass renderPass, 
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& registry);
		~PointLightSystem();

		PointLightSystem(const PointLightSystem&) = delete;
		PointLightSystem& operator=(const PointLightSystem&) = delete;

		void update(GlobalUBO& ubo, float frameTime);
		void render(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts);
		void createPipeline(VkRenderPass renderPass);

		entt::registry& registry;

		std::unique_ptr<BGLPipeline> bglPipeline;
		VkPipelineLayout pipelineLayout;
	};
	
#else
	class PointLightSystem : BGLRenderSystem {
	public:
		PointLightSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry,
			BGLDevice& bglDevice);

		void update(GlobalUBO& ubo, float frameTime);
		void render(FrameInfo& frameInfo);
	private:
		std::unique_ptr<BGLBuffer> uboBuffer;
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};
#endif
}