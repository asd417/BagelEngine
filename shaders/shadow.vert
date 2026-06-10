#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in vec3 position;
// remaining vertex attributes are declared to satisfy the binding layout but not used

const int MAX_LIGHTS = 10;
struct PointLight { vec4 position; vec4 color; };

const int CASCADE_COUNT = 4;

struct DirectionalLight {
    vec4 direction;
    vec4 color;
    mat4 lightSpaceMatrix[CASCADE_COUNT];
    vec4 cascadeSplits;
};

layout(set=0, binding=4) uniform GlobalUBO {
    mat4 projectionMatrix;
    mat4 viewMatrix;
    mat4 inverseViewMatrix;
    vec4 ambientLightColor;
    PointLight pointLights[MAX_LIGHTS];
    uint numLights;
    // std140 auto-aligns vec4 to 544
    vec4 lineColor;
    mat4 invViewProjMatrix;
    float exposure;
    // std140 auto-aligns struct to 640
    DirectionalLight directionalLight;
} ubo;

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
