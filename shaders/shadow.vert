#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"

layout(location=0) in vec3 position;
// remaining vertex attributes are declared to satisfy the binding layout but not used

// CASCADE_COUNT, DirectionalLight, and GlobalUBO (binding 4) come from ubo.glsl via pbr.glsl.

struct ObjectData { mat4 modelMatrix; vec4 scale; };
layout(set=0, binding=5) readonly buffer objTransform {
    ObjectData objects[];
} objTransformArray[];

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    uint BufferedTransformHandle;
    uint UsesBufferedTransform;
    uint cascadeIndex;
} push;

void main() {
    mat4 modelMatrix = push.modelMatrix;
    if (push.UsesBufferedTransform != 0) {
        modelMatrix = objTransformArray[push.BufferedTransformHandle].objects[gl_InstanceIndex].modelMatrix;
    }
    gl_Position = ubo.directionalLight.lightSpaceMatrix[push.cascadeIndex] * modelMatrix * vec4(position, 1.0);
}
