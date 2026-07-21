#version 450

// Planet gbuffer fragment shader. Albedo comes from the elevation colour gradient in the push
// constant, sampled at this fragment's normalized height — no texture maps. Writes the same three
// gbuffer targets as gbuffer_fill, so deferred lighting treats the planet like any opaque surface.

#extension GL_KHR_vulkan_glsl:enable
#extension GL_GOOGLE_include_directive:require
#include "data_transform.glsl" // octEncode

const int MAX_GRADIENT_POINTS = 8; // must match MAX_GRADIENT_POINTS in planet.hpp

layout(location=0) in float fragHeight; // normalized elevation in [0,1]
layout(location=1) in vec3 fragNormalWorld;

layout(location=0) out vec4 outNormal;   // rg = oct-encoded normal, b = roughness, a = metallic
layout(location=1) out vec4 outAlbedo;   // xyz = albedo, w = 1.0 (valid pixel)
layout(location=2) out vec4 outEmission; // xyz = emission RGB, w = unused

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 params;                          // x=radius, y=minElevation, z=maxElevation, w=gradientCount
	vec4 gradient[MAX_GRADIENT_POINTS];   // xyz=colour, w=height key
} push;

// Sample the gradient at height h (stops assumed sorted by height, clamped at the ends).
vec3 sampleGradient(float h) {
	int count = int(push.params.w);
	if (count <= 0) return vec3(1.0);
	if (h <= push.gradient[0].w) return push.gradient[0].xyz;
	if (h >= push.gradient[count - 1].w) return push.gradient[count - 1].xyz;
	for (int i = 1; i < count; i++) {
		if (h <= push.gradient[i].w) {
			float span = max(push.gradient[i].w - push.gradient[i - 1].w, 1e-6);
			float t = (h - push.gradient[i - 1].w) / span;
			return mix(push.gradient[i - 1].xyz, push.gradient[i].xyz, t);
		}
	}
	return push.gradient[count - 1].xyz;
}

void main(){
	vec3 normal = normalize(fragNormalWorld);
	const float roughness = 0.9; // terrain is largely diffuse
	const float metallic  = 0.0;

	outNormal   = vec4(octEncode(normal), roughness, metallic);
	outAlbedo   = vec4(sampleGradient(fragHeight), 1.0);
	outEmission = vec4(0.0);
}
