#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// Adds the bloom result onto the composited swapchain image via additive blending.
// Runs after the transparent pass has written the radiosity buffer, so bloom now
// includes transparent fragments.

layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
    uint  bloomHandle;
    float bloomIntensity;
} push;

void main() {
    vec3 bloom = (push.bloomHandle != 0u)
        ? texture(samplerColor[push.bloomHandle], fragUV).rgb * push.bloomIntensity
        : vec3(0.0);
    // Tonemap + gamma so the additive blend stays within the LDR swapchain range,
    // matching the space the composite wrote the scene in.
    bloom = bloom / (bloom + vec3(1.0));
    bloom = pow(bloom, vec3(1.0 / 2.2));
    outColor = vec4(bloom, 1.0);
}
