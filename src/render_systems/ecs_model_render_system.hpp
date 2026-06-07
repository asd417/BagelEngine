#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "../bagel_camera.hpp"
#include "../bagel_gameobject.hpp"
#include "../bagel_model.hpp"


//#define MODELRENDER_ORIGINAL
namespace bagel {
	struct ECSPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{ 1.0f };

		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform = 0;
		uint32_t albedoMap = 0;
		uint32_t normalMap = 0;
		uint32_t metalRoughMap = 0;
	};

	class ModelRenderSystem : BGLRenderSystem{
	public:
		ModelRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry);
		void renderEntities(FrameInfo& frameInfo);
	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

}