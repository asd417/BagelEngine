#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
    float filterRadius; // UV-space radius for tent kernel
    uint  inputHandle;  // bindless handle of the smaller mip to upsample
} push;

void main() {
    vec2 t = vec2(push.filterRadius) / vec2(textureSize(samplerColor[push.inputHandle], 0));

    // 3x3 tent filter — weights 1 2 1 / 2 4 2 / 1 2 1, sum = 16
    vec3 a = texture(samplerColor[push.inputHandle], fragUV + vec2(-t.x, -t.y)).rgb;
    vec3 b = texture(samplerColor[push.inputHandle], fragUV + vec2( 0.0, -t.y)).rgb;
    vec3 c = texture(samplerColor[push.inputHandle], fragUV + vec2( t.x, -t.y)).rgb;
    vec3 d = texture(samplerColor[push.inputHandle], fragUV + vec2(-t.x,  0.0)).rgb;
    vec3 e = texture(samplerColor[push.inputHandle], fragUV                    ).rgb;
    vec3 f = texture(samplerColor[push.inputHandle], fragUV + vec2( t.x,  0.0)).rgb;
    vec3 g = texture(samplerColor[push.inputHandle], fragUV + vec2(-t.x,  t.y)).rgb;
    vec3 h = texture(samplerColor[push.inputHandle], fragUV + vec2( 0.0,  t.y)).rgb;
    vec3 i = texture(samplerColor[push.inputHandle], fragUV + vec2( t.x,  t.y)).rgb;

    vec3 result = (a + c + g + i) * (1.0 / 16.0)
                + (b + d + f + h) * (2.0 / 16.0)
                + e               * (4.0 / 16.0);

    outColor = vec4(result, 1.0);
}
