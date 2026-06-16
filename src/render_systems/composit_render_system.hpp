#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"

namespace bagel {

	struct CompositionPush {
		float    time            = 0.0f;
		uint32_t debugMode       = 0;     // 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission
		uint32_t bloomHandle     = 0;     // bindless texture handle for the bloom result
		float    bloomIntensity  = 0.08f; // global bloom brightness scale
		uint32_t radiosityHandle = 0;     // bindless texture handle for the HDR radiosity buffer
		uint32_t smaaEdgeHandle   = 0;    // bindless handle for the SMAA edges target (r_drawmode 9)
		uint32_t smaaWeightHandle = 0;    // bindless handle for the SMAA weights target (r_drawmode 10)
	};

	class CompositRenderSystem : BGLRenderSystem {
	public:
		CompositRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry);

		void render(FrameInfo& frameInfo);

		CompositionPush pushParams{};

	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};

} // namespace bagel
