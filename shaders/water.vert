#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_GOOGLE_include_directive : require
#include "ubo.glsl"   // GlobalUBO (binding 4): projection / view matrices

// Vertex layout MUST match BGLModel::Vertex::getAttributeDescriptions() (locations 0-5), same as
// gbuffer_fill.vert / transparent.vert. Water only needs position + normal; the rest are declared
// so the pipeline's vertex input is fully consumed (avoids attribute-mismatch validation errors).
layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec4 tangent;
layout(location=4) in vec2 uv;
layout(location=5) in uint in_materialIndex;

layout(location=0) out vec3 fragPosWorld;
layout(location=1) out vec3 fragNormalWorld;

// Must match the push block in water.frag (and WaterPushConstantData on the C++ side).
layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;
	float time;
} push;

void main() {
	mat3 normalMatrix  = transpose(inverse(mat3(push.modelMatrix)));
	vec4 positionWorld = push.modelMatrix * vec4(position, 1.0);
	gl_Position        = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;

	fragPosWorld    = positionWorld.xyz;
	fragNormalWorld = normalize(normalMatrix * normal);
}
