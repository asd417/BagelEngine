#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_frame_info.hpp"
#include "bagel_render_system.hpp"
#include "engine/bagel_descriptors.hpp"

namespace bagel {

	struct GBufferPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{ 1.0f };
		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform   = 0;
		uint32_t materialRowBase = 0; // skinBase + skinIndex*numSlots; skinTable row for this draw
		float emissionLux = 1.0f;
		uint32_t fallbackAlbedoMap = 0;
	};

	class GBufferRenderSystem : BGLRenderSystem {
	public:
		GBufferRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry);

		void renderEntities(FrameInfo& frameInfo);

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
