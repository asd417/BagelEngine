#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
    float filterRadius; // UV-space radius for tent kernel
    uint  inputHandle;  // bindless handle of the smaller mip to upsample
    float weight;       // contribution weight for this mip level
} push;

vec3 sampleSrc(vec2 uv) {
    vec3 v = texture(samplerColor[push.inputHandle], uv).rgb;
    return (any(isnan(v)) || any(isinf(v))) ? vec3(0.0) : v;
}

void main() {
    vec2 t = vec2(push.filterRadius) / vec2(textureSize(samplerColor[push.inputHandle], 0));

    // 3x3 tent filter — weights 1 2 1 / 2 4 2 / 1 2 1, sum = 16
    vec3 a = sampleSrc(fragUV + vec2(-t.x, -t.y));
    vec3 b = sampleSrc(fragUV + vec2( 0.0, -t.y));
    vec3 c = sampleSrc(fragUV + vec2( t.x, -t.y));
    vec3 d = sampleSrc(fragUV + vec2(-t.x,  0.0));
    vec3 e = sampleSrc(fragUV                    );
    vec3 f = sampleSrc(fragUV + vec2( t.x,  0.0));
    vec3 g = sampleSrc(fragUV + vec2(-t.x,  t.y));
    vec3 h = sampleSrc(fragUV + vec2( 0.0,  t.y));
    vec3 i = sampleSrc(fragUV + vec2( t.x,  t.y));

    vec3 result = (a + c + g + i) * (1.0 / 16.0)
                + (b + d + f + h) * (2.0 / 16.0)
                + e               * (4.0 / 16.0);

    outColor = vec4(result * push.weight, 1.0);
}
