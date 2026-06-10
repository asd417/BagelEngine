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

	// Must match the push block in transparent.vert / transparent.frag
	struct TransparentPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{ 1.0f };
		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform   = 0;
		float emissionLux = 1.0f;
	};

	// Forward, alpha-blended pass for transparent submeshes. Runs in the swapchain render pass
	// after the composite (so it draws on top of the resolved opaque image) and before ImGui.
	// Depth-tests against the opaque depth blitted into the swapchain buffer; does not write depth.
	class TransparentRenderSystem : BGLRenderSystem {
	public:
		TransparentRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager,
			entt::registry& registry);

		void renderEntities(FrameInfo& frameInfo);

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
