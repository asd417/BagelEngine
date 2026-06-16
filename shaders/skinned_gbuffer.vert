#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"
// Skeletal-skinning G-buffer vertex shader. Same outputs as gbuffer_fill.vert (so it reuses
// gbuffer_fill.frag), but it skins the position/normal/tangent with a per-vertex joint blend.
// Per-vertex joints/weights are NOT vertex attributes — they live in an SSBO indexed by
// (skinVertexBase + gl_VertexIndex), keeping the static vertex format untouched.

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
layout(location=2) out vec3 fragTangent;
layout(location=3) out vec3 fragBitangent;
layout(location=4) out vec3 fragNormalWorld;
layout(location=5) flat out VS_OUT vs_out;

// GlobalUBO (binding 4) comes from ubo.glsl via pbr.glsl.

// Global material (skin) table — same as the static pass.
layout(set = 0, binding = 8) readonly buffer SkinTable {
	uvec4 entries[];
} skinTable;

// Per-vertex skin influences: joints packed into one uint (4×u8), weights as unorm4x8.
struct SkinInf { uint joints; uint weights; };
layout(set = 0, binding = 9)  readonly buffer SkinBuf    { SkinInf v[]; } skinBuf;
// Baked joint-matrix palette: m[animBaseOffset + jointIndex].
layout(set = 0, binding = 10) readonly buffer PaletteBuf { mat4 m[];   } palette;

layout(set = 0, binding = 6) uniform sampler2D samplerColor[];

// Layout matches GBufferPushConstantData: offsets 80/84 (the buffered-transform slots in the
// static path) are repurposed here for skinVertexBase/animBaseOffset. The frag (gbuffer_fill)
// only reads materialRowBase/emissionLux/fallbackAlbedoMap at 88/92/96 — unchanged.
layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;
	uint skinVertexBase;
	uint animBaseOffset;
	uint materialRowBase;
	float emissionLux;
	uint fallbackAlbedoMap;
} push;

void main() {
	// Linear blend skinning: sum of weightᵢ · palette[joint ᵢ].
	SkinInf s = skinBuf.v[push.skinVertexBase + gl_VertexIndex];
	uvec4 j = uvec4(s.joints & 0xFFu, (s.joints >> 8) & 0xFFu, (s.joints >> 16) & 0xFFu, (s.joints >> 24) & 0xFFu);
	vec4  w = unpackUnorm4x8(s.weights);
	uint pb = push.animBaseOffset;
	mat4 skin = w.x * palette.m[pb + j.x]
	          + w.y * palette.m[pb + j.y]
	          + w.z * palette.m[pb + j.z]
	          + w.w * palette.m[pb + j.w];

	mat4 modelMatrix  = push.modelMatrix * skin;
	mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
	vec4 positionWorld = modelMatrix * vec4(position, 1.0);
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;

	fragPosWorld    = positionWorld.xyz;
	fragUV          = uv;
	fragNormalWorld = normalize(normalMatrix * normal);
	vs_out.isInstancedTransform = 0;

	uvec4 mat = skinTable.entries[push.materialRowBase + in_materialIndex];
	vs_out.albedoMap     = mat.x;
	vs_out.normalMap     = mat.y;
	vs_out.metalRoughMap = mat.z;
	vs_out.emissionMap   = mat.w;

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
