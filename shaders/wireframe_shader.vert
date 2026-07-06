#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"

// Must match BGLModel::Vertex::getAttributeDescriptions() — same layout as gbuffer_fill.vert.
layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec4 tangent;
layout(location=4) in vec2 uv;
layout(location=5) in uint in_materialIndex;

//layout(location=0) out vec3 fragColor;
layout(location=0) out vec3 fragPosWorld;
layout(location=1) out vec3 fragNormalWorld;
layout(location=2) out vec2 fragUV;
layout(location=3) out int isInstancedTransform;

// GlobalUBO (binding 4) comes from ubo.glsl via pbr.glsl.

struct ObjectData{
	mat4 modelMatrix;
};
layout (set = 0, binding = 5) readonly buffer objTransform {
	ObjectData objects[];
} objTransformArray[];

layout (set = 0, binding = 6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
} push;

//Executed once per vertex
void main() {
	vec3 graphicsPos = vec3(position.x,position.y,position.z);
	mat4 modelMatrix;
	if(push.UsesBufferedTransform != 0){
		modelMatrix = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].modelMatrix;
		isInstancedTransform = 1;
	} else {
		modelMatrix = push.modelMatrix;
		isInstancedTransform = 0;
	}
	// modelMatrix already bakes scale in (TransformComponent::computeMat4 / TransformArrayComponent::mat4),
	// so use it directly — matching gbuffer_fill.vert. Re-applying a separate scale here double-scaled.
	mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));

	vec4 positionWorld = modelMatrix * vec4(graphicsPos,1.0);
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * positionWorld;
	fragNormalWorld = normalize(normalMatrix * normal);
	isInstancedTransform = 0;

	fragPosWorld = positionWorld.xyz;
	fragUV = uv;
}