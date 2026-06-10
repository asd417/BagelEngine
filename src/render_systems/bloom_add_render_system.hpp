#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"

namespace bagel {

	struct BloomAddPush {
		uint32_t bloomHandle    = 0;     // bindless handle for the bloom result (mip 0)
		float    bloomIntensity = 0.08f; // global bloom brightness scale
	};

	// Full-screen additive pass that blends the bloom result onto the swapchain image, after the
	// transparent pass has contributed to the radiosity buffer that bloom is computed from.
	class BloomAddRenderSystem : BGLRenderSystem {
	public:
		BloomAddRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager,
			entt::registry& registry);

		void render(FrameInfo& frameInfo);

		BloomAddPush pushParams{};

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
