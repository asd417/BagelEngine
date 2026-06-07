#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_KHR_vulkan_glsl : enable

struct VS_OUT {
	int isInstancedTransform;
	uint albedoMap;
	uint normalMap;
	uint metalRoughMap;
	uint specularMap;
	uint heightMap;
	uint opacityMap;
	uint aoMap;
	uint refractionMap;
	uint emissionMap;
};

layout(location=0) in vec3 fragPosWorld;
layout(location=1) in vec2 fragUV;
layout(location=2) in vec3 fragTangent;
layout(location=3) in vec3 fragBitangent;
layout(location=4) in vec3 fragNormalWorld;
layout(location=5) flat in VS_OUT fs_in;

layout(location=0) out vec4 outColor;

const int   MAX_LIGHTS = 10;
const float PI         = 3.14159265359;

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
	mat4 modelMatrix;
	vec4 scale;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
	uint albedoMap;
	uint normalMap;
	uint metalRoughMap;
	uint emissionMap;
} push;

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
	uint albedoIdx = (push.albedoMap != 0) ? push.albedoMap : fs_in.albedoMap;

	vec4 albedoSample = (albedoIdx != 0)
		? texture(samplerColor[albedoIdx], fragUV)
		: vec4(fragTangent, 1.0);
	vec3  albedo = albedoSample.rgb;
	float alpha  = albedoSample.a;

	float roughness = 0.5;
	float metallic  = 0.0;

	vec3 normal = normalize(fragNormalWorld);
	uint normalIdx = (push.normalMap != 0) ? push.normalMap : fs_in.normalMap;
	if (normalIdx != 0) {
		mat3 TBN = mat3(fragTangent, fragBitangent, fragNormalWorld);
		vec3 n   = texture(samplerColor[normalIdx], fragUV).rgb * 2.0 - 1.0;
		normal   = normalize(TBN * n);
	}

	uint mrIdx = (push.metalRoughMap != 0) ? push.metalRoughMap : fs_in.metalRoughMap;
	if (mrIdx != 0) {
		vec3 mr  = texture(samplerColor[mrIdx], fragUV).rgb;
		roughness = clamp(mr.g, 0.0, 1.0);
		metallic  = clamp(mr.b, 0.0, 1.0);
	}

	vec3 camPos = ubo.inverseViewMatrix[3].xyz;
	vec3 V      = normalize(camPos - fragPosWorld);
	vec3 F0     = mix(vec3(0.04), albedo, metallic);
	vec3 Lo     = vec3(0.0);

	for (int i = 0; i < int(ubo.numLights); i++) {
		PointLight pl  = ubo.pointLights[i];
		vec3 toLight   = pl.position.xyz - fragPosWorld;
		float atten    = 1.0 / dot(toLight, toLight);
		vec3  L        = normalize(toLight);
		vec3  H        = normalize(V + L);
		vec3  radiance = pl.color.xyz * pl.color.w * atten;

		vec3  F   = FresnelSchlick(max(dot(H,V),0.0), F0);
		float NDF = DistributionGGX(normal, H, roughness);
		float G   = GeometrySmith(normal, V, L, roughness);
		vec3  sp  = (NDF*G*F) / (4.0*max(dot(normal,V),0.0)*max(dot(normal,L),0.0) + 0.0001);
		vec3  kD  = (vec3(1.0)-F)*(1.0-metallic);
		Lo += (kD*albedo/PI + sp) * radiance * max(dot(normal,L), 0.0);
	}

	vec3 ambient  = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w * albedo;

	vec3 emission = vec3(0.0);
	uint emissionIdx = (push.emissionMap != 0) ? push.emissionMap : fs_in.emissionMap;
	if (emissionIdx != 0)
		emission = texture(samplerColor[emissionIdx], fragUV).rgb;

	vec3 color = ambient + Lo + emission;
	color = color / (color + vec3(1.0));
	color = pow(color, vec3(1.0 / 2.2));

	outColor = vec4(color, alpha);
}
