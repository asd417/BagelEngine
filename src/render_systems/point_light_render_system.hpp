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
#ifdef POINTLIGHT_ORIGINAL
	class PointLightSystem {
	public:
		PointLightSystem(
			BGLDevice& device, 
			VkRenderPass renderPass, 
			std::vector<VkDescriptorSetLayout> setLayouts, entt::registry& registry);
		~PointLightSystem();

		PointLightSystem(const PointLightSystem&) = delete;
		PointLightSystem& operator=(const PointLightSystem&) = delete;

		void update(GlobalUBO& ubo, float frameTime);
		void render(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts);
		void createPipeline(VkRenderPass renderPass);

		BGLDevice& bglDevice;
		entt::registry& registry;

		std::unique_ptr<BGLPipeline> bglPipeline;
		VkPipelineLayout pipelineLayout;

	};
#endif
	struct PointLightPushConstant {
		glm::vec4 positions{};
		glm::vec4 color{};
		float radius;
	};

	class PointLightSystem : BGLRenderSystem {
	public:
		PointLightSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry) :
			BGLRenderSystem{ renderPass, setLayouts, sizeof(PointLightPushConstant) },
			descriptorManager{ _descriptorManager },
			registry{ _registry }
		{
			createPipeline(renderPass, "/shaders/point_light.vert.spv", "/shaders/point_light.frag.spv", nullptr);
		}

		void update(GlobalUBO& ubo, float frameTime);
		void render(FrameInfo& frameInfo);
	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};
	
}