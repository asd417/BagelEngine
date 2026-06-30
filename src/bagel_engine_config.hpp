#pragma once

// Engine-wide tunable limits. One home for the magic counts that used to be
// re-#defined per translation unit. Several mirror GPU-side array sizes / descriptor
// budgets — keep them in sync with the matching shader constants when you change them.

#define GLOBAL_DESCRIPTOR_COUNT 1000 // bindless descriptor table size
#define GLOBAL_UBO_COUNT        10   // global UBO slots in the descriptor pool
#define MAX_LIGHTS              10   // point lights uploaded per frame (mirrors the shader)
#define MAX_TRANSFORM_PER_ENT  1000 // capacity of TransformArrayComponent's fixed arrays

// Factory defaults for the live-tunable Settings panel. Single source of truth: these
// double as the initializers for the owning class/component members AND the values the
// per-slider "Reset" buttons restore, so the two can never drift apart.
namespace bagel::cfg
{
	// Camera controller
	inline constexpr float kMouseSensitivity = 0.1f;
	inline constexpr float kMoveSpeed        = 10.0f;
	// Camera projection
	inline constexpr float kCameraNear       = 0.1f;
	inline constexpr float kCameraFar        = 300.0f;
	inline constexpr float kCameraFovDegrees = 100.0f; // horizontal FOV
	// Directional-light shadow cascades
	inline constexpr float kCascade0End      = 7.0f;
	inline constexpr float kCascade1End      = 11.0f;
	inline constexpr float kCascade2End      = 15.0f;
	inline constexpr float kCascade3End      = 30.0f;
	inline constexpr float kCasterRange      = 400.0f;
	inline constexpr float kShadowBiasMin    = 0.002f;
	inline constexpr float kShadowBiasSlope  = 0.005f;
	// Bloom / tonemap
	inline constexpr bool  kBloomEnabled     = true;
	inline constexpr float kBloomIntensity   = 0.054f;
	inline constexpr float kBloomThreshold   = 0.16f;
	inline constexpr float kBloomMipDecay    = 0.5f;
	inline constexpr float kExposure         = 0.0075f;
	// SMAA edge detection
	inline constexpr int   kSmaaEdgeMethod          = 0; // 0 = luma
	inline constexpr float kSmaaEdgeThreshold       = 0.05f;
	inline constexpr float kSmaaLocalContrastAdapt  = 2.0f;
}
