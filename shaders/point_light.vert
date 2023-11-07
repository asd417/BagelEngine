#version 450

#extension GL_KHR_vulkan_glsl : enable

layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 uv;
layout(location=4) in int texture_index;

const vec2 OFFSETS[6] = vec2[](
	vec2(-1.0,-1.0),
	vec2(-1.0, 1.0),
	vec2( 1.0,-1.0),
	vec2( 1.0,-1.0),
	vec2(-1.0, 1.0),
	vec2( 1.0, 1.0)
);

layout (location = 0) out vec2 fragOffset;

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
layout (binding = 1) uniform sampler2D samplerColor;

//This is obviously inefficient because I can just use w of position in PointLight as a radius but for the sake of demonstrating pushcontant
layout(push_constant) uniform Push {
	vec4 position;
	vec4 color;
	float radius;
} push;

void main(){
	fragOffset = OFFSETS[gl_VertexIndex];
	vec3 cameraRightWorld = {ubo.viewMatrix[0][0],ubo.viewMatrix[1][0],ubo.viewMatrix[2][0]};
	vec3 cameraUpWorld = {ubo.viewMatrix[0][1],ubo.viewMatrix[1][1],ubo.viewMatrix[2][1]};

	vec3 positionWorld = push.position.xyz + push.radius * fragOffset.x * cameraRightWorld + push.radius * fragOffset.y * cameraUpWorld;
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * vec4(positionWorld,1.0);

}