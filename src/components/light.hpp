#pragma once

#include <glm/glm.hpp>

namespace bagel {
	struct PointLightComponent {
		glm::vec4 color = { 1.0f,1.0f,1.0f,1.0f }; // rgb=color, w unused
		float radius = 1.0f;
		float bloomHaloRadius = 1.0f;
		float lux = 800.0f; // luminous intensity in lux; multiplied by exposure in radiosity shader
	};

	struct DirectionalLightComponent {
		glm::vec4 color    = { 1.0f, 1.0f, 1.0f, 1.0f }; // rgb = color, w unused (driven by lux)
		glm::vec3 rotation = { 45.0f, 0.0f, 0.0f };       // pitch/yaw/roll in degrees; positive pitch aims the light downward (+Y)
		float lux          = 3000.0f;                      // luminous intensity (like PointLightComponent::lux)
		glm::vec4 cascadeEnds = { 7.0f, 11.0f, 15.0f, 30.0f }; // view-space distance where each shadow cascade ends (must be increasing)
		float casterRange  = 400.0f;                       // how far behind a cascade slice geometry can still cast into it
		float shadowBiasMin   = 0.002f;                    // minimum depth bias applied in the shadow compare
		float shadowBiasSlope = 0.005f;                    // slope-scaled bias, max at grazing N.L
	};
}
