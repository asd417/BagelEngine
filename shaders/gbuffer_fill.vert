#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec4 tangent;
layout(location=4) in vec2 uv;
layout(location=5) in uint in_albedoMap;
layout(location=6) in uint in_normalMap;
layout(location=7) in uint in_metalRoughMap;
layout(location=8) in uint in_specularMap;
layout(location=9) in uint in_heightMap;
layout(location=10) in uint in_opacityMap;
layout(location=11) in uint in_aoMap;
layout(location=12) in uint in_refractionMap;
layout(location=13) in uint in_emissionMap;

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

layout(location=0) out vec3 fragPosWorld;
layout(location=1) out vec2 fragUV;
// When albedoMap == 0: fragTangent carries vertex color
// When albedoMap != 0: fragTangent = world tangent, fragBitangent = world bitangent (reconstructed per-vertex)
layout(location=2) out vec3 fragTangent;
layout(location=3) out vec3 fragBitangent;
layout(location=4) out vec3 fragNormalWorld;
layout(location=5) flat out VS_OUT vs_out;

struct PointLight {
	vec4 position;
	vec4 color;
};

const int MAX_LIGHTS = 10;
layout(set = 0, binding = 4) uniform GlobalUBO {
	mat4 projectionMatrix;
	mat4 viewMatrix;
	mat4 inverseViewMatrix;
	vec4 ambientLightColor;
	PointLight pointLights[MAX_LIGHTS];
	uint numLights;
	vec4 lineColor;
} ubo;

struct ObjectData {
	mat4 modelMatrix;
	vec4 scale;
};
layout(set = 0, binding = 5) readonly buffer objTransform {
	ObjectData objects[];
} objTransformArray[];

layout(set = 0, binding = 6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
} push;

void main() {
	mat4 modelMatrix;
	vec4 scale;

	if (push.UsesBufferedTransform != 0) {
		scale       = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].scale;
		modelMatrix = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].modelMatrix;
		vs_out.isInstancedTransform = 1;
	} else {
		scale       = push.scale;
		modelMatrix = push.modelMatrix;
		vs_out.isInstancedTransform = 0;
	}

	mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
	vec4 positionWorld = modelMatrix * vec4(position, 1.0);
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;

	fragPosWorld    = positionWorld.xyz;
	fragUV          = uv;
	fragNormalWorld = normalize(normalMatrix * normal);

	vs_out.albedoMap     = in_albedoMap;
	vs_out.normalMap     = in_normalMap;
	vs_out.metalRoughMap = in_metalRoughMap;
	vs_out.specularMap   = in_specularMap;
	vs_out.heightMap     = in_heightMap;
	vs_out.opacityMap    = in_opacityMap;
	vs_out.aoMap         = in_aoMap;
	vs_out.refractionMap = in_refractionMap;
	vs_out.emissionMap   = in_emissionMap;

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
