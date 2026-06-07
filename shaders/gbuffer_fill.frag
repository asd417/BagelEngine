#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_KHR_vulkan_glsl : enable

struct VS_OUT {
	int isInstancedTransform;
	uint albedoMap;
	uint normalMap;
	uint metalRoughMap;
	uint specularMap;
	uint heightMap;
	uint opacityMap;
	uint aoMap;
	uint refractionMap;
	uint emissionMap;
};

layout(location=0) in vec3 fragPosWorld;
layout(location=1) in vec2 fragUV;
// fragTangent doubles as vertex color when albedoMap == 0
layout(location=2) in vec3 fragTangent;
layout(location=3) in vec3 fragBitangent;
layout(location=4) in vec3 fragNormalWorld;
layout(location=5) flat in VS_OUT fs_in;

layout(location=0) out vec4 outNormal;    // rg = oct-encoded normal, b = roughness, a = metallic
layout(location=1) out vec4 outAlbedo;    // xyz = albedo, w = 1.0 (valid pixel)
layout(location=2) out vec4 outEmission;  // xyz = emission RGB, w = unused

layout(set = 0, binding = 6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
	uint albedoMap;
	uint normalMap;
	uint metalRoughMap;
	uint emissionMap;
	float emissionLux;
	uint fallbackAlbedoMap;
} push;

vec2 signNotZero(vec2 v) {
	return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}
vec2 octEncode(vec3 n) {
	float l1 = abs(n.x) + abs(n.y) + abs(n.z);
	vec2 p = n.xy / l1;
	if (n.z < 0.0) p = (1.0 - abs(p.yx)) * signNotZero(p);
	return p * 0.5 + 0.5;
}

void main() {
	// Albedo: per-surface texture → fallback grid → vertex color
	uint effectiveAlbedo = (fs_in.albedoMap != 0) ? fs_in.albedoMap : push.fallbackAlbedoMap;
	vec3 albedo = texture(samplerColor[effectiveAlbedo], fragUV).xyz;

	float metallic  = 0.0;
	float roughness = 0.5;

	vec3 normal = normalize(fragNormalWorld);
	if (fs_in.normalMap != 0) {
		vec3 T = normalize(fragTangent);
		vec3 B = normalize(fragBitangent);
		mat3 TBN = mat3(T, B, normal);
		vec3 n   = texture(samplerColor[fs_in.normalMap], fragUV).rgb;
		n        = n * 2.0 - 1.0;
		normal   = normalize(TBN * n);
	}
	// glTF ORM: R=occlusion, G=roughness, B=metallic
	if (fs_in.metalRoughMap != 0) {
		vec3 mr  = texture(samplerColor[fs_in.metalRoughMap], fragUV).rgb;
		roughness = clamp(mr.g, 0.0, 1.0);
		metallic  = clamp(mr.b, 0.0, 1.0);
	}

	vec4 emissionOut = vec4(0.0);
	if (push.emissionMap != 0) {
		vec3 emission = texture(samplerColor[push.emissionMap], fragUV).rgb;
		emissionOut = vec4(emission, clamp(push.emissionLux / 10000.0, 0.0, 1.0));
	}

	outNormal   = vec4(octEncode(normal), roughness, metallic);
	outAlbedo   = vec4(albedo, 1.0);
	outEmission = emissionOut;
}
