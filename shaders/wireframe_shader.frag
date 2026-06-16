#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_KHR_vulkan_glsl : enable
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"

//layout(location=0) in vec3 fragColor;
layout(location=0) in vec3 fragPosWorld;
layout(location=1) in vec3 fragNormalWorld;
layout(location=2) in vec2 fragUV;
layout(location=3) flat in int isInstancedTransform;


layout(location=0) out vec4 outColor;

const float gamma = 2.2;

// GlobalUBO (binding 4) comes from ubo.glsl via pbr.glsl.

struct ObjectData{
	mat4 modelMatrix;
	vec4 scale;
};
layout (set = 0, binding = 5) readonly buffer objTransform {
	ObjectData objects[];
} objTransformArray[];

layout (set = 0, binding = 6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
	vec4 color;
} push;

void main(){
	outColor = (push.color.a > 0.0) ? push.color : ubo.lineColor;
}