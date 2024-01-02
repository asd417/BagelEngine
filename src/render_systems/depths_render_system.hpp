#pragma once

#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "../bgl_camera.hpp"
#include "../bgl_gameobject.hpp"
#include "../bgl_model.hpp"

//#define MODELRENDER_ORIGINAL
namespace bagel {
	struct DepthsPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::mat4 normalMatrix{ 1.0f };
	};

	class ModelRenderSystem : BGLRenderSystem {
	public:
		ModelRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry) :
			BGLRenderSystem{ renderPass, setLayouts, sizeof(DepthsPushConstantData) },
			descriptorManager{ _descriptorManager },
			registry{ _registry }
		{
			createPipeline(renderPass, "/shaders/simple_shader.vert.spv", "/shaders/simple_shader.frag.spv", nullptr);
		}
		void renderEntities(FrameInfo& frameInfo);
	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

}