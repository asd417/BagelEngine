#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outBloom;

layout(set=0, binding=0) uniform sampler2D gPosition;
layout(set=0, binding=2) uniform sampler2D gAlbedo;
layout(set=0, binding=3) uniform sampler2D gEmission;

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

layout(set=0, binding=6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	float    blurRadius;    // texel stride
	float    intensity;     // brightness multiplier
	uint     blurDir;       // 0=horizontal, 1=vertical (final pass)
	uint     inputHandle;   // 0=sample gEmission, else samplerColor[inputHandle]
} push;

// 9-tap separable Gaussian weights (σ≈2, sum=1)
const float W[5] = float[](0.22702702, 0.19459459, 0.12162162, 0.05405405, 0.01621622);

vec3 sampleInput(vec2 uv) {
	return (push.inputHandle != 0u)
		? texture(samplerColor[push.inputHandle], uv).rgb
		: texture(gEmission, uv).rgb;
}

void main() {
	vec2 texSize = (push.inputHandle != 0u)
		? vec2(textureSize(samplerColor[push.inputHandle], 0))
		: vec2(textureSize(gEmission, 0));

	vec2 step = (push.blurDir == 0u)
		? vec2(push.blurRadius / texSize.x, 0.0)
		: vec2(0.0, push.blurRadius / texSize.y);

	vec3 bloom = sampleInput(fragUV) * W[0];
	for (int i = 1; i < 5; i++) {
		float fi = float(i);
		bloom += (sampleInput(fragUV + step * fi) + sampleInput(fragUV - step * fi)) * W[i];
	}
	bloom *= push.intensity;

	// Point light halos — only on the vertical (final) pass so they're in the output
	if (push.blurDir == 1u) {
		vec4 albedoData = texture(gAlbedo, fragUV);
		bool hasGeometry = albedoData.w > 0.5;

		for (int i = 0; i < int(ubo.numLights); i++) {
			PointLight pl  = ubo.pointLights[i];
			vec3 lightColor = pl.color.rgb * pl.color.w;

			if (hasGeometry) {
				vec3 fragPos = texture(gPosition, fragUV).xyz;
				vec3 toLight = pl.position.xyz - fragPos;
				float distSq = dot(toLight, toLight);
				bloom += lightColor * push.intensity * 0.04 / (distSq + 1.0);
			} else {
				vec4 clipPos = ubo.projectionMatrix * ubo.viewMatrix * vec4(pl.position.xyz, 1.0);
				if (clipPos.w <= 0.0) continue;
				clipPos /= clipPos.w;
				vec2 lightUV = clipPos.xy * 0.5 + 0.5;
				float d = length(fragUV - lightUV);
				bloom += lightColor * exp(-d * d * 180.0) * 0.6;
			}
		}
	}

	outBloom = vec4(bloom, 1.0);
}
