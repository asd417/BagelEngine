#version 450

// Planet gbuffer vertex shader. Planets are shaded by an elevation colour gradient (no texture
// maps); the gradient is evaluated per-fragment. Here we compute the fragment's normalized
// elevation key [0,1] and forward it with the world-space normal. Vertex colour is unused.

#extension GL_KHR_vulkan_glsl : enable
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl" // GlobalUBO (binding 4): projection/view matrices

const int MAX_GRADIENT_POINTS = 8; // must match MAX_GRADIENT_POINTS in planet.hpp

layout(location=0) in vec3 position;
layout(location=1) in vec3 color; // unused
layout(location=2) in vec3 normal;

layout(location=0) out float fragHeight; // normalized elevation in [0,1]
layout(location=1) out vec3 fragNormalWorld;

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 params;                          // x=radius, y=minElevation, z=maxElevation, w=gradientCount
	vec4 gradient[MAX_GRADIENT_POINTS];   // xyz=colour, w=height key
} push;

void main() {
	mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
	vec4 positionWorld = push.modelMatrix * vec4(position, 1.0);
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;

	// position is model-space (unit * radius * (1 + noise)), so |position|/radius - 1 = elevation.
	float elevation = length(position) / push.params.x - 1.0;
	float range = max(push.params.z - push.params.y, 1e-6);
	fragHeight = clamp((elevation - push.params.y) / range, 0.0, 1.0);

	fragNormalWorld = normalize(normalMatrix * normal);
}
