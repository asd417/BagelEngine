#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in vec2 fragUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform sampler2D gPosition; // xyz=world pos, w=metallic
layout(set=0, binding=1) uniform sampler2D gNormal;   // xyz=world normal, w=roughness
layout(set=0, binding=2) uniform sampler2D gAlbedo;   // xyz=albedo, w=1 if valid
layout(set=0, binding=3) uniform sampler2D gEmission;   // xyz=raw surface emission (debug only)
layout(set=0, binding=6) uniform sampler2D samplerColor[]; // bindless texture array

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

layout(push_constant) uniform Push {
	float    time;
	uint     debugMode;   // 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission
	uint     bloomHandle; // bindless handle into samplerColor[] for the bloom result
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
	vec4 posData    = texture(gPosition, fragUV);
	vec4 normData   = texture(gNormal,   fragUV);
	vec4 albedoData = texture(gAlbedo,   fragUV);

	vec3 bloom = (push.bloomHandle != 0u)
		? texture(samplerColor[push.bloomHandle], fragUV).rgb
		: vec3(0.0);

	if (albedoData.w < 0.5) {
		outColor = vec4(0.01 + bloom, 1.0);
		return;
	}

	vec3  fragPos   = posData.xyz;
	float metallic  = posData.w;
	vec3  N         = normalize(normData.xyz);
	float roughness = normData.w;
	vec3  albedo    = albedoData.xyz;

	// G-buffer debug views — r_drawmode <n> in console
	if (push.debugMode == 1u) { outColor = vec4(albedo, 1.0);                        return; }
	if (push.debugMode == 2u) { outColor = vec4(N * 0.5 + 0.5, 1.0);                return; }
	if (push.debugMode == 3u) { outColor = vec4(fract(fragPos * 0.15 + 0.5), 1.0);   return; }
	if (push.debugMode == 4u) { outColor = vec4(vec3(roughness), 1.0);               return; }
	if (push.debugMode == 5u) { outColor = vec4(vec3(metallic),  1.0);               return; }
	if (push.debugMode == 6u) { outColor = vec4(bloom, 1.0);                         return; }
	if (push.debugMode == 7u) { outColor = vec4(texture(gEmission, fragUV).rgb, 1.0); return; }

	vec3 camPos = ubo.inverseViewMatrix[3].xyz;
	vec3 V      = normalize(camPos - fragPos);
	vec3 F0     = mix(vec3(0.04), albedo, metallic);
	vec3 Lo     = vec3(0.0);

	for (int i = 0; i < int(ubo.numLights); i++) {
		PointLight pl  = ubo.pointLights[i];
		vec3 toLight   = pl.position.xyz - fragPos;
		float atten    = 1.0 / dot(toLight, toLight);
		vec3  L        = normalize(toLight);
		vec3  H        = normalize(V + L);
		vec3  radiance = pl.color.xyz * pl.color.w * atten;

		vec3  F   = FresnelSchlick(max(dot(H,V),0.0), F0);
		float NDF = DistributionGGX(N, H, roughness);
		float G   = GeometrySmith(N, V, L, roughness);
		vec3  sp  = (NDF*G*F) / (4.0*max(dot(N,V),0.0)*max(dot(N,L),0.0) + 0.0001);
		vec3  kD  = (vec3(1.0)-F)*(1.0-metallic);
		Lo += (kD*albedo/PI + sp) * radiance * max(dot(N,L), 0.0);
	}

	vec3 ambient = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w * albedo;
	vec3 color   = ambient + Lo + bloom;

	// Reinhard tone mapping + gamma correction
	color = color / (color + vec3(1.0));
	color = pow(color, vec3(1.0 / 2.2));

	outColor = vec4(color, 1.0);
}
