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

//layout(location=0) out vec3 fragColor;
layout(location=0) out vec3 fragPosWorld;
layout(location=1) out vec3 fragNormalWorld;
layout(location=2) out vec2 fragUV;
layout(location=3) out int isInstancedTransform;

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
	mat4 normalMatrix;
	vec4 scale;
	if(push.UsesBufferedTransform != 0){
		scale = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].scale;
		modelMatrix = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].modelMatrix;
		isInstancedTransform = 1;
	} else {
		scale = push.scale;
		modelMatrix = push.modelMatrix;
		isInstancedTransform = 0;
	}
	normalMatrix = modelMatrix;
	
	modelMatrix[0] = modelMatrix[0] * scale.x;
	modelMatrix[1] = modelMatrix[1] * scale.y;
	modelMatrix[2] = modelMatrix[2] * scale.z;
	normalMatrix[0] = normalMatrix[0] * (1/scale.x);
	normalMatrix[1] = normalMatrix[1] * (1/scale.y);
	normalMatrix[2] = normalMatrix[2] * (1/scale.z);

	positionWorld = modelMatrix * vec4(graphicsPos,1.0);
	vec4 pos = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;
	//pos.y = -1 * pos.y;
	gl_Position = pos;
	vec4 invScale = vec4(1/push.scale.x, 1/push.scale.y, 1/push.scale.z, 1);
	//scale model matrix with inverse scale to get normalMatrix
	fragNormalWorld = normalize(mat3(normalMatrix) * normal);
	isInstancedTransform = 0;

	fragPosWorld = positionWorld.xyz;
	fragUV = uv;
}