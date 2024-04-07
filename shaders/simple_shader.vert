#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec3 tangent;
layout(location=4) in vec3 bitangent;
layout(location=5) in vec2 uv;
layout(location=6) in uint in_albedoMap;
layout(location=7) in uint in_normalMap;
layout(location=8) in uint in_roughMap;
layout(location=9) in uint in_metallicMap;
layout(location=10) in uint in_specularMap;
layout(location=11) in uint in_heightMap;
layout(location=12) in uint in_opacityMap;
layout(location=13) in uint in_aoMap;
layout(location=14) in uint in_refractionMap;
layout(location=15) in uint in_emissionMap;

// layout(location=0) out mat3 TBN;
// layout(location=1) out vec3 fragPosWorld;
// layout(location=2) out vec3 fragTangent;
// layout(location=3) out vec3 fragBitangent;
// layout(location=4) out vec3 fragNormalWorld;
// layout(location=5) out vec2 fragUV;
// layout(location=6) out int isInstancedTransform;

// layout(location=7) out uint albedoMap;
// layout(location=8) out uint normalMap;
// layout(location=9) out uint roughMap;
// layout(location=10) out uint metallicMap;
// layout(location=11) out uint specularMap;
// layout(location=12) out uint heightMap;
// layout(location=13) out uint opacityMap;
// layout(location=14) out uint aoMap;
// layout(location=15) out uint refractionMap;
// layout(location=16) out uint emissionMap;

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

layout(location=0) out vec3 fragPosWorld;
layout(location=1) out vec2 fragUV;
layout(location=2) out vec3 fragTangent;
layout(location=3) out vec3 fragBitangent;
layout(location=4) out vec3 fragNormalWorld;
layout(location=5) flat out VS_OUT vs_out;

struct PointLight {
	vec4 position; // ignore w
	vec4 color; // w intensity
};

const int MAX_LIGHTS = 10; //Must match value in bagel_frame_info.hpp
layout(set = 0, binding = 0) uniform GlobalUBO {
	mat4 projectionMatrix;
	mat4 viewMatrix;
	mat4 inverseViewMatrix;
	vec4 ambientLightColor;

	PointLight pointLights[MAX_LIGHTS]; //Can use 'specialization constants' to set the size of this array at pipeline creation
	uint numLights;

// Line color for wireframe
	vec4 lineColor;
} ubo;

struct ObjectData{
	mat4 modelMatrix;
	vec4 scale;
};
layout (set = 0, binding = 1) readonly buffer objTransform {
	ObjectData objects[];
} objTransformArray[];

layout (set = 0, binding = 2) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;

	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
} push;


//Executed once per vertex
void main() {
	vec4 positionWorld;
	vec3 graphicsPos = vec3(position.x,position.y,position.z);
	mat4 modelMatrix;
	mat3 normalMatrix;
	vec4 scale;
	if(push.UsesBufferedTransform != 0){
		scale = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].scale;
		modelMatrix = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].modelMatrix;
		vs_out.isInstancedTransform = 1;
	} else {
		scale = push.scale;
		modelMatrix = push.modelMatrix;
		vs_out.isInstancedTransform = 0;
	}
	
	normalMatrix = transpose(inverse(mat3(modelMatrix)));

	positionWorld = modelMatrix * vec4(graphicsPos,1.0);
	vec4 pos = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;
	gl_Position = pos;
	
	vec3 T = normalize(normalMatrix * tangent);
	vec3 B = normalize(normalMatrix * cross(tangent,normal));
	vec3 N = normalize(normalMatrix * normal);

	//Reorthogonize tangent vector
	fragPosWorld = positionWorld.xyz;
	fragUV = uv;
	fragTangent = T;
	fragBitangent = B;
	fragNormalWorld = N;
	
	vs_out.isInstancedTransform = 0;

	//orthogonal matrices (each axis is a perpendicular unit vector) is that the transpose of an orthogonal matrix equals its inverse.
	//Use it to move light pos and view dir to tangent space

	vs_out.albedoMap = in_albedoMap;
	vs_out.normalMap = in_normalMap;
	vs_out.roughMap = in_roughMap;
	vs_out.metallicMap = in_metallicMap;
	vs_out.specularMap = in_specularMap;
	vs_out.heightMap = in_heightMap;
	vs_out.opacityMap = in_opacityMap;
	vs_out.aoMap = in_aoMap;
	vs_out.refractionMap = in_refractionMap;
	vs_out.emissionMap = in_emissionMap;
}