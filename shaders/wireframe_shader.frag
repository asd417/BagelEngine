#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_KHR_vulkan_glsl : enable

//layout(location=0) in vec3 fragColor;
layout(location=0) in vec3 fragPosWorld;
layout(location=1) in vec3 fragNormalWorld;
layout(location=2) in vec2 fragUV;
layout(location=3) flat in int fragIndex;
layout(location=4) flat in int isInstancedTransform;


layout(location=0) out vec4 outColor;

struct PointLight {
	vec4 position; // ignore w
	vec4 color; // w intensity
};

const float gamma = 2.2;
const float PI = 3.1415926538;
const int MAX_LIGHTS = 10; //Must match value in bagel_frame_info.hpp

layout(set = 0, binding = 0) uniform GlobalUBO {
	mat4 projectionMatrix;
	mat4 viewMatrix;
	mat4 inverseViewMatrix;
	vec4 ambientLightColor;
	PointLight pointLights[MAX_LIGHTS]; //Can use 'specialization constants' to set the size of this array at pipeline creation
	int numLights;
} ubo;

struct ObjectData{
	mat4 modelMatrix;
	mat4 normalMatrix;
};

// layout (set = 0, binding = 1) readonly buffer objTransform {
// 	ObjectData objects[];
// } objTransformArray[];

// layout (set = 0, binding = 2) uniform sampler2D samplerColor[];
//layout (set = 1, binding = 0) uniform sampler2D samplerModelColor;

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	mat4 normalMatrix;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
} push;

void main(){
	outColor = vec4(1.0,1.0,1.0,1.0);
}