// Shared GlobalUBO (set 0, binding 4) and its light structs — the single shader-side source
// of truth for the per-frame uniform block. Mirrors bagel::GlobalUBO in bagel_frame_info.hpp;
// the std140 offsets there are guarded by static_asserts. Every shader includes this (directly
// or transitively via pbr.glsl) instead of redeclaring the block. The including shader must
// declare a #version before including this file.
#ifndef UBO_GLSL
#define UBO_GLSL

const int MAX_LIGHTS    = 10; // must match MAX_LIGHTS in bagel_frame_info.hpp
const int CASCADE_COUNT = 4;  // must match SHADOW_CASCADE_COUNT in bagel_frame_info.hpp

// std140: position.xyz + maxDistance occupy one 16-byte slot, so color lands at offset 16.
struct PointLight {
    vec3  position;
    float maxDistance; // max influence distance (world units); = position.w slot
    vec4  color;       // xyz = color, w = intensity (lux)
};

struct DirectionalLight {
    vec4 direction;                        // xyz = world-space forward direction of the light
    vec4 color;                            // xyz = color, w = intensity
    mat4 lightSpaceMatrix[CASCADE_COUNT];  // per-cascade light view-projection
    vec4 cascadeSplits;                    // view-space distance where each cascade ends
};

// Declared in full so the layout always matches bagel::GlobalUBO. std140 still lets a shader
// reference only the leading members it needs and ignore the rest.
layout(set = 0, binding = 4) uniform GlobalUBO {
    mat4 projectionMatrix;
    mat4 viewMatrix;
    mat4 inverseViewMatrix;
    vec4 ambientLightColor;
    PointLight pointLights[MAX_LIGHTS];
    uint numLights;
    vec4 lineColor;
    mat4 invViewProjMatrix;
    float exposure;
    DirectionalLight directionalLight;
    uint hasDirLight;
    uint shadowMapHandle;
    float shadowBiasMin;
    float shadowBiasSlope;
} ubo;

#endif // UBO_GLSL
