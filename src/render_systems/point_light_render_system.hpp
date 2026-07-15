#pragma once

#include <memory>
#include <vector>
#include <glm/gtc/constants.hpp>

#include "bagel_buffer.hpp"
#include "bagel_frame_info.hpp"
#include "bagel_render_system.hpp"
#include "engine/bagel_descriptors.hpp"
#include "engine/bagel_engine_device.hpp"

//#define POINTLIGHT_ORIGINAL
namespace bagel {

	struct PointLightPushConstant {
		glm::vec4 positions{};
		glm::vec4 color{};
		float radius;
	};

	// Gathers point-light data into the GlobalUBO for the deferred lighting pass.
	// Does not draw anything (the old billboard render pass was removed).
	class PointLightSystem : BGLRenderSystem {
	public:
		PointLightSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry,
			BGLDevice& bglDevice);

		void update(GlobalUBO& ubo, float frameTime);
	private:
		std::unique_ptr<BGLBuffer> uboBuffer;
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};
}