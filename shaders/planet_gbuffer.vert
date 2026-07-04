#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_GOOGLE_include_directive : require
#include "ubo.glsl"

// Uses the standard BGLModel::Vertex layout; the planet only needs position + normal.
// Locations 1 (color), 3 (tangent), 4 (uv), 5 (materialIndex) are supplied by the
// vertex format but ignored here.
layout(location=0) in vec3 position;
layout(location=2) in vec3 normal;

layout(location=0) out vec3 fragPosWorld;
layout(location=1) out vec3 fragNormalWorld;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    vec4 centerRadius; // xyz = planet center (world), w = base radius
    vec4 noiseF;       // amplitude, frequency, lacunarity, gain (unused in VS)
    vec4 noiseF2;      // sealevel, octaves, seed, epsilon       (unused in VS)
} push;

void main() {
    vec4 worldPos   = push.modelMatrix * vec4(position, 1.0);
    fragPosWorld    = worldPos.xyz;
    mat3 normalMat  = transpose(inverse(mat3(push.modelMatrix)));
    fragNormalWorld = normalize(normalMat * normal);
    gl_Position     = ubo.projectionMatrix * ubo.viewMatrix * worldPos;
}
