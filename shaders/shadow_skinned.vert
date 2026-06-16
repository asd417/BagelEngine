#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"

// Skinned shadow caster: same depth-only output as shadow.vert, but the position is skinned
// with the joint palette so the shadow silhouette matches the animated (deformed) mesh.

layout(location=0) in vec3 position;
// remaining vertex attributes are declared by the binding layout but unused here

// CASCADE_COUNT, DirectionalLight, and GlobalUBO (binding 4) come from ubo.glsl via pbr.glsl.

// Skinning SSBOs — same bindings as the skinned g-buffer pass.
struct SkinInf { uint joints; uint weights; };
layout(set=0, binding=9)  readonly buffer SkinBuf    { SkinInf v[]; } skinBuf;
layout(set=0, binding=10) readonly buffer PaletteBuf { mat4 m[];   } palette;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    uint skinVertexBase;
    uint animBaseOffset;
    uint cascadeIndex;
} push;

void main() {
    SkinInf s = skinBuf.v[push.skinVertexBase + gl_VertexIndex];
    uvec4 j = uvec4(s.joints & 0xFFu, (s.joints >> 8) & 0xFFu, (s.joints >> 16) & 0xFFu, (s.joints >> 24) & 0xFFu);
    vec4  w = unpackUnorm4x8(s.weights);
    uint pb = push.animBaseOffset;
    mat4 skin = w.x * palette.m[pb + j.x]
              + w.y * palette.m[pb + j.y]
              + w.z * palette.m[pb + j.z]
              + w.w * palette.m[pb + j.w];

    gl_Position = ubo.directionalLight.lightSpaceMatrix[push.cascadeIndex]
                * push.modelMatrix * skin * vec4(position, 1.0);
}
