#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"

layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec4 tangent;
layout(location=4) in vec2 uv;
layout(location=5) in uint in_materialIndex;

struct VS_OUT {
	int isInstancedTransform;
	uint albedoMap;
	uint normalMap;
	uint metalRoughMap;
	uint emissionMap;
};

layout(location=0) out vec3 fragPosWorld;
layout(location=1) out vec2 fragUV;
// When albedoMap == 0: fragTangent carries vertex color
// When albedoMap != 0: fragTangent = world tangent, fragBitangent = world bitangent (reconstructed per-vertex)
layout(location=2) out vec3 fragTangent;
layout(location=3) out vec3 fragBitangent;
layout(location=4) out vec3 fragNormalWorld;
layout(location=5) flat out VS_OUT vs_out;

// GlobalUBO (binding 4) comes from ubo.glsl via pbr.glsl.

struct ObjectData {
	mat4 modelMatrix;
};
layout(set = 0, binding = 5) readonly buffer objTransform {
	ObjectData objects[];
} objTransformArray[];

// Global skin table: per-model blocks of uvec4 entries (bindless handles: x=albedo,
// y=normal, z=metalRough, w=emission). The material for this draw is
//   skinTable.entries[ push.materialRowBase + in_materialIndex(=local slot) ].
layout(set = 0, binding = 8) readonly buffer SkinTable {
	uvec4 entries[];
} skinTable;

layout(set = 0, binding = 6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
	uint materialRowBase;
} push;

void main() {
	mat4 modelMatrix;

	if (push.UsesBufferedTransform != 0) {
		modelMatrix = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].modelMatrix;
		vs_out.isInstancedTransform = 1;
	} else {
		modelMatrix = push.modelMatrix;
		vs_out.isInstancedTransform = 0;
	}

	mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
	vec4 positionWorld = modelMatrix * vec4(position, 1.0);
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;

	fragPosWorld    = positionWorld.xyz;
	fragUV          = uv;
	fragNormalWorld = normalize(normalMatrix * normal);

	uvec4 mat = skinTable.entries[push.materialRowBase + in_materialIndex];
	vs_out.albedoMap     = mat.x;
	vs_out.normalMap     = mat.y;
	vs_out.metalRoughMap = mat.z;
	vs_out.emissionMap   = mat.w;

	// When no albedo texture is bound, repurpose fragTangent to carry vertex color.
	if (vs_out.albedoMap == 0) {
		fragTangent   = color;
		fragBitangent = vec3(0.0);
	} else {
		vec3 N = fragNormalWorld;
		vec3 T = normalize(normalMatrix * tangent.xyz);
		fragTangent   = T;
		fragBitangent = cross(N, T) * tangent.w;
	}
}
