#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
    float    threshold;   // luminance cutoff (nonzero only on first mip)
    float    intensity;   // output brightness scale
    uint     inputHandle; // bindless handle of the source texture (radiosity or previous mip)
} push;

vec3 sampleSrc(vec2 uv) {
    return texture(samplerColor[push.inputHandle], uv).rgb;
}

vec3 applyThreshold(vec3 c) {
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c * max(lum - push.threshold, 0.0) / max(lum, 1e-4);
}

// Karis average: down-weights bright firefly pixels to prevent them from dominating
float karisWeight(vec3 c) {
    return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)));
}

vec3 karisAvg(vec3 a, vec3 b, vec3 c, vec3 d) {
    float wa = karisWeight(a), wb = karisWeight(b), wc = karisWeight(c), wd = karisWeight(d);
    return (a*wa + b*wb + c*wc + d*wd) / (wa + wb + wc + wd);
}

void main() {
    vec2 srcSize = vec2(textureSize(samplerColor[push.inputHandle], 0));
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

    vec3 result;
    if (push.threshold > 0.0) {
        // First mip only: apply threshold per-sample, then Karis-weighted average
        // This prevents bright specular spikes from spreading into large bloom patches
        vec3 tA = applyThreshold(A), tB = applyThreshold(B), tC = applyThreshold(C);
        vec3 tD = applyThreshold(D), tE = applyThreshold(E), tF = applyThreshold(F);
        vec3 tG = applyThreshold(G), tH = applyThreshold(H), tI = applyThreshold(I);
        vec3 tJ = applyThreshold(J), tK = applyThreshold(K), tL = applyThreshold(L);
        vec3 tM = applyThreshold(M);

        result  = karisAvg(tD, tE, tI, tJ) * 0.5;
        result += karisAvg(tA, tB, tG, tF) * 0.125;
        result += karisAvg(tB, tC, tH, tG) * 0.125;
        result += karisAvg(tF, tG, tL, tK) * 0.125;
        result += karisAvg(tG, tH, tM, tL) * 0.125;
    } else {
        // Subsequent mips: standard weighted average, no threshold
        result  = (D + E + I + J) * 0.125;
        result += (A + B + G + F) * 0.03125;
        result += (B + C + H + G) * 0.03125;
        result += (F + G + L + K) * 0.03125;
        result += (G + H + M + L) * 0.03125;
    }

    // Single firefly/NaN guard on the result (was per-tap): a non-finite HDR source texel
    // poisons the average, so drop the whole output texel instead of 13 per-tap checks.
    result *= push.intensity;
    if (any(isnan(result)) || any(isinf(result))) result = vec3(0.0);
    outColor = vec4(result, 1.0);
}
