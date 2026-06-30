#pragma once

#include <glm/glm.hpp>

#include "../bagel_engine_config.hpp"

namespace bagel {
	struct PointLightComponent {
		glm::vec4 color = { 1.0f,1.0f,1.0f,1.0f }; // rgb=color, w unused
		float lux = 800.0f; // luminous intensity in lux; multiplied by exposure in radiosity shader
	};

	struct DirectionalLightComponent {
		glm::vec4 color    = { 1.0f, 1.0f, 1.0f, 1.0f }; // rgb = color, w unused (driven by lux)
		glm::vec3 rotation = { 45.0f, 0.0f, 0.0f };       // pitch/yaw/roll in degrees; positive pitch aims the light downward (+Y)
		float lux          = 3000.0f;                      // luminous intensity (like PointLightComponent::lux)
		glm::vec4 cascadeEnds = { cfg::kCascade0End, cfg::kCascade1End, cfg::kCascade2End, cfg::kCascade3End }; // view-space distance where each shadow cascade ends (must be increasing)
		float casterRange  = cfg::kCasterRange;            // how far behind a cascade slice geometry can still cast into it
		float shadowBiasMin   = cfg::kShadowBiasMin;       // minimum depth bias applied in the shadow compare
		float shadowBiasSlope = cfg::kShadowBiasSlope;     // slope-scaled bias, max at grazing N.L
	};
}
