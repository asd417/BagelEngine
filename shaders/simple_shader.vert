#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

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
layout(location=4) out int isInstancedTransform;

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

struct ObjectData{
	mat4 modelMatrix;
	mat4 normalMatrix;
};
layout (set = 0, binding = 1) readonly buffer objTransform {
	ObjectData objects[];
} objTransformArray[];

layout (set = 0, binding = 2) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	mat4 normalMatrix;

	uint diffuseTextureHandle; 		//1
	uint emissionTextureHandle; 	//2 Emission texture uses alpha channel as brightness. 1.0f = brightest
	uint normalTextureHandle;		//4
	uint roughmetalTextureHandle;	//8
	uint textureMapFlag;

	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
} push;


//Executed once per vertex
void main() {
	vec4 positionWorld;
	vec3 graphicsPos = vec3(position.x,position.y,position.z);
	//Converts vertex position to world position
	if(push.UsesBufferedTransform != 0){
		mat4 modelMatrix = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].modelMatrix;
		positionWorld = modelMatrix * vec4(graphicsPos,1.0);
		//Converts vertex position to screen space
		vec4 pos = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;
		//pos.y = -1 * pos.y;
		gl_Position = pos;
		fragNormalWorld = normalize(mat3(objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].normalMatrix) * normal);
		isInstancedTransform = 1;
	} else {
		mat4 modelMatrix = push.modelMatrix;
		positionWorld = modelMatrix * vec4(graphicsPos,1.0);
		vec4 pos = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;
		//pos.y = -1 * pos.y;
		gl_Position = pos;
		fragNormalWorld = normalize(mat3(push.normalMatrix) * normal);
		isInstancedTransform = 0;
	}
	fragPosWorld = positionWorld.xyz;
	fragUV = uv;
	fragIndex = texture_index;
}