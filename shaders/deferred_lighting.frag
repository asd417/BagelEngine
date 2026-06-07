#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform sampler2D gDepth;
layout(set=0, binding=1) uniform sampler2D gNormal;
layout(set=0, binding=2) uniform sampler2D gAlbedo;
layout(set=0, binding=3) uniform sampler2D gEmission;
layout(set=0, binding=6) uniform sampler2D samplerColor[];

const int MAX_LIGHTS = 10;
struct PointLight { vec4 position; vec4 color; };

layout(set=0, binding=4) uniform GlobalUBO {
    mat4 projectionMatrix;
    mat4 viewMatrix;
    mat4 inverseViewMatrix;
    vec4 ambientLightColor;
    PointLight pointLights[MAX_LIGHTS];
    uint numLights;
    vec4 lineColor;
    mat4 invViewProjMatrix;
} ubo;

layout(push_constant) uniform Push {
    float    time;
    uint     debugMode;       // 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission
    uint     bloomHandle;     // bindless handle for the bloom result
    float    bloomIntensity;
    uint     radiosityHandle; // bindless handle for the HDR radiosity buffer
} push;

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

void main() {
    vec4 albedoData = texture(gAlbedo, fragUV);

    vec3 bloom = (push.bloomHandle != 0u)
        ? texture(samplerColor[push.bloomHandle], fragUV).rgb * push.bloomIntensity
        : vec3(0.0);

    if (albedoData.w < 0.5) {
        outColor = vec4(0.01 + bloom, 1.0);
        return;
    }

    vec4  normData  = texture(gNormal, fragUV);
    vec3  N         = octDecode(normData.rg);
    float roughness = normData.b;
    float metallic  = normData.a;
    vec3  albedo    = albedoData.xyz;

    if (push.debugMode == 1u) { outColor = vec4(albedo, 1.0);                         return; }
    if (push.debugMode == 2u) { outColor = vec4(N * 0.5 + 0.5, 1.0);                 return; }
    if (push.debugMode == 3u) {
        float depth  = texture(gDepth, fragUV).r;
        vec3 fragPos = reconstructWorldPos(fragUV, depth);
        outColor = vec4(fract(fragPos * 0.15 + 0.5), 1.0);
        return;
    }
    if (push.debugMode == 4u) { outColor = vec4(vec3(roughness), 1.0);                return; }
    if (push.debugMode == 5u) { outColor = vec4(vec3(metallic),  1.0);                return; }
    if (push.debugMode == 6u) { outColor = vec4(bloom, 1.0);                          return; }
    if (push.debugMode == 7u) { outColor = vec4(texture(gEmission, fragUV).rgb, 1.0); return; }
    if (push.debugMode == 8u) {
        // Raw radiosity buffer (HDR, pre-tonemap)
        vec3 raw = (push.radiosityHandle != 0u)
            ? texture(samplerColor[push.radiosityHandle], fragUV).rgb
            : vec3(0.0);
        outColor = vec4(raw * 0.1, 1.0); // scale down so HDR values are visible
        return;
    }

    vec3 color = (push.radiosityHandle != 0u)
        ? texture(samplerColor[push.radiosityHandle], fragUV).rgb
        : albedo * 0.05;
    color += bloom;

    // Reinhard tonemap + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
