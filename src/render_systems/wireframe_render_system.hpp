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
	struct WireframePushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{};
		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform = 0;
	};

	class WireframeRenderSystem : BGLRenderSystem {
	public:
		WireframeRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry);

		void renderEntities(FrameInfo& frameInfo);
	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;

		bool drawCollision = true;
	};

}