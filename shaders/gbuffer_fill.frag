#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_KHR_vulkan_glsl : enable

struct VS_OUT {
	int isInstancedTransform;
	uint albedoMap;
	uint normalMap;
	uint roughMap;
	uint metallicMap;
	uint specularMap;
	uint heightMap;
	uint opacityMap;
	uint aoMap;
	uint refractionMap;
	uint emissionMap;
};

layout(location=0) in vec3 fragPosWorld;
layout(location=1) in vec2 fragUV;
// fragTangent doubles as vertex color when albedoMap == 0 (set in vertex shader)
layout(location=2) in vec3 fragTangent;
layout(location=3) in vec3 fragBitangent;
layout(location=4) in vec3 fragNormalWorld;
layout(location=5) flat in VS_OUT fs_in;

layout(location=0) out vec4 outPosition;  // xyz = world pos,   w = metallic
layout(location=1) out vec4 outNormal;    // xyz = world normal, w = roughness
layout(location=2) out vec4 outAlbedo;    // xyz = albedo,       w = 1.0 (valid pixel)
layout(location=3) out vec4 outEmission;  // xyz = emission RGB, w = unused

layout(set = 0, binding = 6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
	uint albedoMap;
	uint normalMap;
	uint roughMap;
	uint emissionMap;
} push;

void main() {
	// Albedo: texture if bound, otherwise vertex color passed through fragTangent
	vec3 albedo = (fs_in.albedoMap != 0)
		? texture(samplerColor[fs_in.albedoMap], fragUV).xyz
		: fragTangent;

	float metallic  = 0.0;
	float roughness = 0.5;

	vec3 normal = normalize(fragNormalWorld);
	if (fs_in.normalMap != 0) {
		mat3 TBN = mat3(fragTangent, fragBitangent, fragNormalWorld);
		vec3 n   = texture(samplerColor[fs_in.normalMap], fragUV).rgb;
		n        = n * 2.0 - 1.0;
		normal   = normalize(TBN * n);
	}
	if (fs_in.roughMap != 0) {
		roughness = clamp(texture(samplerColor[fs_in.roughMap], fragUV).x, 0.0, 1.0);
	}
	if (fs_in.metallicMap != 0) {
		metallic = clamp(texture(samplerColor[fs_in.metallicMap], fragUV).x, 0.0, 1.0);
	}

	vec3 emission = vec3(0.0);
	if (push.emissionMap != 0) {
		emission = texture(samplerColor[push.emissionMap], fragUV).rgb;
	}

	outPosition = vec4(fragPosWorld, metallic);
	outNormal   = vec4(normal, roughness);
	outAlbedo   = vec4(albedo, 1.0);
	outEmission = vec4(emission, 0.0);
}
