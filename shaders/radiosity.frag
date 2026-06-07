#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"

layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform sampler2D gDepth;
layout(set=0, binding=1) uniform sampler2D gNormal;
layout(set=0, binding=2) uniform sampler2D gAlbedo;
layout(set=0, binding=3) uniform sampler2D gEmission;

// Froxel Radiance Cascade 0 — indirect diffuse GI
layout(set=1, binding=0) uniform sampler3D cascade0;

// MAX_LIGHTS comes from ubo.glsl (via pbr.glsl). PI is not provided there, so define it here.
const float PI = 3.14159265359;

// One shadow map per cascade, each at its own resolution
layout(set=0, binding=7) uniform sampler2DShadow shadowMaps[CASCADE_COUNT];

// GlobalUBO (binding 4) comes from ubo.glsl via pbr.glsl.

// 3x3 PCF via hardware compare (LESS_OR_EQUAL). Returns 1.0 = fully lit, 0.0 = fully shadowed.
// cascade diverges per fragment, so indexing the sampler array needs nonuniformEXT.
float sampleShadow(vec2 uv, int cascade, float refDepth) {
    const int filterSize = 5;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMaps[nonuniformEXT(cascade)], 0));
    float shadow = 0.0;
    for (int x = -(filterSize-1)/2; x <= (filterSize-1)/2; ++x) {
        for (int y = -(filterSize-1)/2; y <= (filterSize-1)/2; ++y) {
            shadow += texture(shadowMaps[nonuniformEXT(cascade)], vec3(uv + vec2(x,y)*texelSize, refDepth));
        }
    }
    return shadow / float(filterSize*filterSize);
}

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clip  = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = ubo.invViewProjMatrix * clip;
    return world.xyz / world.w;
}

void main() {
    vec4 albedoData = texture(gAlbedo, fragUV);

    if (albedoData.w < 0.5) {
        outColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float depth    = texture(gDepth,    fragUV).r;
    vec4  normData = texture(gNormal,   fragUV);
    vec4  emData   = texture(gEmission, fragUV);

    vec3  fragPosWorld   = reconstructWorldPos(fragUV, depth);
    vec3  normal         = octDecode(normData.rg);
    float roughness = normData.b;
    float metallic  = normData.a;
    vec3  albedo    = albedoData.xyz;

    vec3 camPos = ubo.inverseViewMatrix[3].xyz;
    vec3 V      = normalize(camPos - fragPosWorld);
    vec3 F0     = mix(vec3(0.04), albedo, metallic);
    vec3 Lo     = vec3(0.0);

    // Point lights
    for (int i = 0; i < int(ubo.numLights); i++) {
        Lo += calculatePointLight(ubo.pointLights[i], fragPosWorld, ubo.exposure, normal, V, albedo, F0, roughness, metallic);
    }

    // Directional light + cascaded shadows. Each lightSpaceMatrix is an ortho view-projection
    // (w stays 1). No Y flip when mapping NDC -> UV: each cascade is written and read with the
    // same matrix. shadowFactor only attenuates the directional contribution; ambient stays outside.
    if (ubo.hasDirLight != 0) {
        vec3 L = normalize(-ubo.directionalLight.direction.xyz);
        vec3 radiance = ubo.directionalLight.color.xyz * ubo.directionalLight.color.w * ubo.exposure;

        // Pick the cascade by view-space depth (the camera images along -z of view space)
        float viewDepth = -(ubo.viewMatrix * vec4(fragPosWorld, 1.0)).z;
        int cascade = CASCADE_COUNT - 1;
        if      (viewDepth < ubo.directionalLight.cascadeSplits.x) cascade = 0;
        else if (viewDepth < ubo.directionalLight.cascadeSplits.y) cascade = 1;
        else if (viewDepth < ubo.directionalLight.cascadeSplits.z) cascade = 2;

        float shadowFactor = 1.0;
        if (viewDepth < ubo.directionalLight.cascadeSplits.w) {
            vec4 lsPos = ubo.directionalLight.lightSpaceMatrix[cascade] * vec4(fragPosWorld, 1.0);
            vec2 shadowUV = lsPos.xy * 0.5 + 0.5;
            if (shadowUV.x > 0.0 && shadowUV.x < 1.0 &&
                shadowUV.y > 0.0 && shadowUV.y < 1.0 &&
                lsPos.z > 0.0 && lsPos.z < 1.0) {
                float bias = max(ubo.shadowBiasSlope * (1.0 - dot(normal, L)), ubo.shadowBiasMin);
                shadowFactor = sampleShadow(shadowUV, cascade, lsPos.z - bias);
            }
        }

        Lo += shadowFactor * pbrDirectLight(normal, V, L, radiance, albedo, F0, roughness, metallic);
    }

    // Indirect diffuse from Froxel Radiance Cascade 0
    {
        vec4 clip    = ubo.projectionMatrix * ubo.viewMatrix * vec4(fragPosWorld, 1.0);
        vec3 ndc     = clip.xyz / clip.w;
        // NDC XY in [-1,1] → [0,1]; NDC Z is already [0,1] in Vulkan
        vec3 uvw     = vec3(ndc.xy * 0.5 + 0.5, ndc.z);
        vec3 cascadeIrr = texture(cascade0, clamp(uvw, 0.0, 1.0)).rgb;
        vec3 F   = FresnelSchlick(max(dot(normal, V), 0.0), F0);
        vec3 kD  = (1.0 - F) * (1.0 - metallic);
        Lo += kD * albedo / PI * cascadeIrr;
    }

    vec3 ambient  = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w * albedo;
    vec3 emission = emData.rgb * emData.a * 10000.0 * ubo.exposure;
    vec3 color    = ambient + Lo + emission;

    outColor = vec4(color, 1.0);
}
