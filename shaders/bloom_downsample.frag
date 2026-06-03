#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform sampler2D gPosition;
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
} ubo;

layout(push_constant) uniform Push {
    float    threshold;   // luminance cutoff (0 = no threshold)
    float    intensity;   // output brightness scale
    uint     inputHandle; // 0 = gEmission + point-light halos, else samplerColor[inputHandle]
} push;

vec3 sampleSrc(vec2 uv) {
    return (push.inputHandle != 0u)
        ? texture(samplerColor[push.inputHandle], uv).rgb
        : texture(gEmission, uv).rgb;
}

vec3 applyThreshold(vec3 c) {
    if (push.threshold <= 0.0) return c;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c * max(lum - push.threshold, 0.0) / max(lum, 1e-4);
}

void main() {
    vec2 srcSize = (push.inputHandle != 0u)
        ? vec2(textureSize(samplerColor[push.inputHandle], 0))
        : vec2(textureSize(gEmission, 0));
    vec2 t = 1.0 / srcSize;

    // 13-tap Kawase dual filter (Jimenez 2014)
    vec3 D = sampleSrc(fragUV + vec2(-0.5, -0.5) * t);
    vec3 E = sampleSrc(fragUV + vec2( 0.5, -0.5) * t);
    vec3 I = sampleSrc(fragUV + vec2(-0.5,  0.5) * t);
    vec3 J = sampleSrc(fragUV + vec2( 0.5,  0.5) * t);

    vec3 A = sampleSrc(fragUV + vec2(-1.0, -1.0) * t);
    vec3 B = sampleSrc(fragUV + vec2( 0.0, -1.0) * t);
    vec3 C = sampleSrc(fragUV + vec2( 1.0, -1.0) * t);
    vec3 F = sampleSrc(fragUV + vec2(-1.0,  0.0) * t);
    vec3 G = sampleSrc(fragUV);
    vec3 H = sampleSrc(fragUV + vec2( 1.0,  0.0) * t);
    vec3 K = sampleSrc(fragUV + vec2(-1.0,  1.0) * t);
    vec3 L = sampleSrc(fragUV + vec2( 0.0,  1.0) * t);
    vec3 M = sampleSrc(fragUV + vec2( 1.0,  1.0) * t);

    vec3 result = (D + E + I + J) * 0.125;
    result += (A + B + G + F) * 0.03125;
    result += (B + C + H + G) * 0.03125;
    result += (F + G + L + K) * 0.03125;
    result += (G + H + M + L) * 0.03125;

    result = applyThreshold(result);

    // Point light halos — only on the first pass (reading from gEmission)
    if (push.inputHandle == 0u) {
        vec4 albedoData = texture(gAlbedo, fragUV);
        bool hasGeometry = albedoData.w > 0.5;

        for (int li = 0; li < int(ubo.numLights); li++) {
            PointLight pl = ubo.pointLights[li];
            vec3 lightColor = pl.color.rgb * pl.color.w;

            if (hasGeometry) {
                vec3 fragPos = texture(gPosition, fragUV).xyz;
                vec3 toLight = pl.position.xyz - fragPos;
                float distSq = dot(toLight, toLight);
                result += lightColor * 0.04 / (distSq + 1.0);
            } else {
                vec4 clipPos = ubo.projectionMatrix * ubo.viewMatrix * vec4(pl.position.xyz, 1.0);
                if (clipPos.w <= 0.0) continue;
                clipPos /= clipPos.w;
                vec2 lightUV = clipPos.xy * 0.5 + 0.5;
                float d = length(fragUV - lightUV);
                result += lightColor * exp(-d * d * 60.0) * 0.6;
            }
        }
    }

    outColor = vec4(result * push.intensity, 1.0);
}
