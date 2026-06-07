#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "../bagel_model.hpp"

namespace bagel {

	// Same push constant layout as ModelRenderSystem / simple_shader
	struct GBufferPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{ 1.0f };
		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform   = 0;
		uint32_t albedoMap        = 0;
		uint32_t normalMap        = 0;
		uint32_t metalRoughMap    = 0;
		uint32_t emissionMap      = 0;
		float    emissionLux      = 800.0f;
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
