#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform sampler2D gDepth;
layout(set=0, binding=1) uniform sampler2D gNormal;
layout(set=0, binding=2) uniform sampler2D gAlbedo;
layout(set=0, binding=3) uniform sampler2D gEmission;
const int   MAX_LIGHTS    = 10;
const int   CASCADE_COUNT = 4;
const float PI            = 3.14159265359;

// One shadow map per cascade, each at its own resolution
layout(set=0, binding=7) uniform sampler2DShadow shadowMaps[CASCADE_COUNT];

struct PointLight { vec4 position; vec4 color; };

struct DirectionalLight {
    vec4 direction;       // xyz = world-space forward direction of the light
    vec4 color;           // xyz = color, w = intensity
    mat4 lightSpaceMatrix[CASCADE_COUNT];
    vec4 cascadeSplits;   // view-space distance where each cascade ends
};

layout(set=0, binding=4) uniform GlobalUBO {
    mat4 projectionMatrix;
    mat4 viewMatrix;
    mat4 inverseViewMatrix;
    vec4 ambientLightColor;
    PointLight pointLights[MAX_LIGHTS];
    uint numLights;
    // std140 auto-aligns vec4 lineColor to 544
    vec4 lineColor;
    mat4 invViewProjMatrix;
    float exposure;
    // std140 auto-aligns DirectionalLight to 640
    DirectionalLight directionalLight;
    uint hasDirLight;
    uint shadowMapHandle;
    float shadowBiasMin;
    float shadowBiasSlope;
} ubo;

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

vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}
vec3 octDecode(vec2 p) {
    p = p * 2.0 - 1.0;
    vec3 n = vec3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * signNotZero(n.xy);
    return normalize(n);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a2    = roughness*roughness*roughness*roughness;
    float NdotH = max(dot(N,H), 0.0);
    float denom = (NdotH*NdotH*(a2-1.0)+1.0);
    return a2 / (PI*denom*denom);
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0-F0)*pow(clamp(1.0-cosTheta,0.0,1.0),5.0);
}
float GeomGGX(float n, float r) {
    float k = (r+1.0)*(r+1.0)/8.0;
    return n/(n*(1.0-k)+k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float r) {
    return GeomGGX(max(dot(N,V),0.0),r) * GeomGGX(max(dot(N,L),0.0),r);
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

    vec3  fragPos   = reconstructWorldPos(fragUV, depth);
    vec3  N         = octDecode(normData.rg);
    float roughness = normData.b;
    float metallic  = normData.a;
    vec3  albedo    = albedoData.xyz;

    vec3 camPos = ubo.inverseViewMatrix[3].xyz;
    vec3 V      = normalize(camPos - fragPos);
    vec3 F0     = mix(vec3(0.04), albedo, metallic);
    vec3 Lo     = vec3(0.0);

    // Point lights
    for (int i = 0; i < int(ubo.numLights); i++) {
        PointLight pl  = ubo.pointLights[i];
        vec3 toLight   = pl.position.xyz - fragPos;
        float atten    = 1.0 / dot(toLight, toLight);
        vec3  L        = normalize(toLight);
        vec3  H        = normalize(V + L);
        vec3  radiance = pl.color.xyz * pl.color.w * ubo.exposure * atten;

        vec3  F   = FresnelSchlick(max(dot(H,V),0.0), F0);
        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  sp  = (NDF*G*F) / (4.0*max(dot(N,V),0.0)*max(dot(N,L),0.0) + 0.0001);
        vec3  kD  = (vec3(1.0)-F)*(1.0-metallic);
        Lo += (kD*albedo/PI + sp) * radiance * max(dot(N,L), 0.0);
    }

    // Directional light + cascaded shadows. Each lightSpaceMatrix is an ortho view-projection
    // (w stays 1). No Y flip when mapping NDC -> UV: each cascade is written and read with the
    // same matrix. shadowFactor only attenuates the directional contribution; ambient stays outside.
    if (ubo.hasDirLight != 0) {
        vec3 L = normalize(-ubo.directionalLight.direction.xyz);
        vec3 H = normalize(V + L);
        vec3 radiance = ubo.directionalLight.color.xyz * ubo.directionalLight.color.w * ubo.exposure;

        // Pick the cascade by view-space depth (the camera images along -z of view space)
        float viewDepth = -(ubo.viewMatrix * vec4(fragPos, 1.0)).z;
        int cascade = CASCADE_COUNT - 1;
        if      (viewDepth < ubo.directionalLight.cascadeSplits.x) cascade = 0;
        else if (viewDepth < ubo.directionalLight.cascadeSplits.y) cascade = 1;
        else if (viewDepth < ubo.directionalLight.cascadeSplits.z) cascade = 2;

        float shadowFactor = 1.0;
        if (viewDepth < ubo.directionalLight.cascadeSplits.w) {
            vec4 lsPos = ubo.directionalLight.lightSpaceMatrix[cascade] * vec4(fragPos, 1.0);
            vec2 shadowUV = lsPos.xy * 0.5 + 0.5;
            if (shadowUV.x > 0.0 && shadowUV.x < 1.0 &&
                shadowUV.y > 0.0 && shadowUV.y < 1.0 &&
                lsPos.z > 0.0 && lsPos.z < 1.0) {
                float bias = max(ubo.shadowBiasSlope * (1.0 - dot(N, L)), ubo.shadowBiasMin);
                shadowFactor = sampleShadow(shadowUV, cascade, lsPos.z - bias);
            }
        }

        vec3  F   = FresnelSchlick(max(dot(H,V),0.0), F0);
        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  sp  = (NDF*G*F) / (4.0*max(dot(N,V),0.0)*max(dot(N,L),0.0) + 0.0001);
        vec3  kD  = (vec3(1.0)-F)*(1.0-metallic);
        Lo += shadowFactor * (kD*albedo/PI + sp) * radiance * max(dot(N,L), 0.0);
    }

    vec3 ambient  = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w * albedo;
    vec3 emission = emData.rgb * emData.a * 10000.0 * ubo.exposure;
    vec3 color    = ambient + Lo + emission;

    outColor = vec4(color, 1.0);
}
