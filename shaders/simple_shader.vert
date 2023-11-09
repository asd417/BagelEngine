#version 450

#extension GL_KHR_vulkan_glsl : enable

layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 uv;
layout(location=4) in int texture_index;

//layout(location=0) out vec3 fragColor;
layout(location=0) out vec3 fragPosWorld;
layout(location=1) out vec3 fragNormalWorld;
layout(location=2) out vec2 fragUV;
layout(location=3) out int fragIndex;

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
	int numLights;
} ubo;
layout (set = 0, binding = 1) uniform sampler2D GlobalUBOColor;

layout(push_constant) uniform Push { 
	mat4 modelMatrix;
	mat4 normalMatrix;
} push;

//Executed once per vertex
void main() {
	//Converts vertex position to world position
	vec4 positionWorld = push.modelMatrix * vec4(position,1.0);
	//Converts vertex position to screen space
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;
	fragNormalWorld = normalize(mat3(push.normalMatrix) * normal);
	fragPosWorld = positionWorld.xyz;
	fragUV = uv;
	fragIndex = texture_index;
}