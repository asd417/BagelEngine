#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_KHR_vulkan_glsl : enable

// layout(location=0) mat3 TBN;
// layout(location=1) in vec3 fragPosWorld;
// layout(location=2) in vec3 fragTangent;
// layout(location=3) in vec3 fragBitangent;
// layout(location=4) in vec3 fragNormalWorld;
// layout(location=5) in vec2 fragUV;
// layout(location=6) flat in int isInstancedTransform;

// layout(location=7) flat in uint albedoMap;
// layout(location=8) flat in uint normalMap;
// layout(location=9) flat in uint roughMap;
// layout(location=10) flat in uint metallicMap;
// layout(location=11) flat in uint specularMap;
// layout(location=12) flat in uint heightMap;
// layout(location=13) flat in uint opacityMap;
// layout(location=14) flat in uint aoMap;
// layout(location=15) flat in uint refractionMap;
// layout(location=16) flat in uint emissionMap;

struct VS_OUT {
	int isInstancedTransform;
	uint albedoMap;
	uint normalMap;
	uint roughMap;
	uint metallicMap;
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


const float gamma = 2.2;
const float PI = 3.14159265359;

struct PointLight {
	vec4 position; // ignore w
	vec4 color; // w intensity
};

const int MAX_LIGHTS = 10; //Must match value in bagel_frame_info.hpp
layout(set = 0, binding = 0) uniform GlobalUBO {
	mat4 projectionMatrix;
	mat4 viewMatrix;
	mat4 inverseViewMatrix;
	vec4 ambientLightColor;

	PointLight pointLights[MAX_LIGHTS]; //Can use 'specialization constants' to set the size of this array at pipeline creation
	uint numLights;

// Line color for wireframe
	vec4 lineColor;
} ubo;

struct ObjectData{
	mat4 modelMatrix;
	vec4 scale;
};

layout (set = 0, binding = 1) readonly buffer objTransform {
	ObjectData objects[];
} objTransformArray[];

layout (set = 0, binding = 2) uniform sampler2D samplerColor[];
//layout (set = 1, binding = 0) uniform sampler2D samplerModelColor;

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 scale;

	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
} push;

// Normal distribution function
// Describes how much of the microfacets are aligned to the half angle vector, influenced by roughness
// a = roughness
// h = halfway vector between the surface normal and light direction
// N = surface normal
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

//Fresnel
//vec3 F0 = vec3(0.04);
//F0 = mix(F0, surfaceColor.rgb, metalness);
//float cosTheta = dot(lightDir, N); 
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
} 

// Geometry function Schlick-GGX
// V = view vector
// L = light direction vector
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

void main(){
	
	//Default values for when there is no texture input
	vec3 diffuse = vec3(0.5f,0.5f,0.5f); //Default grey
	vec4 emission = vec4(0.0f,0.0f,0.0f,0.0f);
	vec3 normal = normalize(fragNormalWorld); //fragNormalWorld is default, using vertex normal interpolated across face
	float metallic = 1.0f; //fully metallic by default. (for specular and normal map inspection) Uses X value
	float roughness = 0.5f; //Half roughness by default. Uses Y value

	//Create new axis with tangent, bitangent, normal
	mat3 TBN = mat3(fragTangent,fragBitangent,fragNormalWorld);
	if(fs_in.albedoMap != 0){
		diffuse = texture(samplerColor[fs_in.albedoMap], vec2(fragUV.x,fragUV.y)).xyz;
	}
	if(fs_in.emissionMap != 0){
		emission = texture(samplerColor[fs_in.emissionMap], vec2(fragUV.x,fragUV.y));
	}
	if(fs_in.normalMap != 0){
		normal = texture(samplerColor[fs_in.normalMap], vec2(fragUV.x,fragUV.y)).rgb; //0~1 range
		normal = normal * 2.0 - 1.0;   //-1.0~1.0 range
		normal = normalize(TBN * normal); //move the normal to world space
	}
	if(fs_in.roughMap != 0){
		vec3 roughness_v = texture(samplerColor[fs_in.roughMap], vec2(fragUV.x,fragUV.y)).xyz;
		roughness = clamp(roughness_v.x,0.0,1.0);
	}
	if(fs_in.metallicMap != 0){
		vec3 metallic_v = texture(samplerColor[fs_in.metallicMap], vec2(fragUV.x,fragUV.y)).xyz;
		metallic = clamp(metallic_v.x,0.0,1.0);
	}

	vec3 albedo = diffuse;
	vec3 cameraPosWorld = ubo.inverseViewMatrix[3].xyz;
	vec3 N = normal;
	vec3 V = normalize(cameraPosWorld - fragPosWorld); //view vector
	vec3 Lo = vec3(0.0);
	vec3 F0 = mix(vec3(0.04), albedo, metallic); 

	for(int i = 0;i<ubo.numLights;i++){
		// Per-light radiance
		PointLight light = ubo.pointLights[i];
		vec3 L_pos = light.position.xyz;
		//L_pos.y = L_pos.y * -1;
		vec3 L = L_pos - fragPosWorld;
		float attenuation = 1.0 / dot(L, L); // dot(L, L) = magnitude(L)^2
		L = normalize(L);
		vec3 H = normalize(V + L); // Half Angle
		vec3 radiance = light.color.xyz * light.color.w * attenuation; //color * brightness * attenuation

		// cook-torrance brdf
		vec3 F 	  = FresnelSchlick(max(dot(H, V), 0.0), F0);
		float NDF = DistributionGGX(N, H, roughness);       
		float G   = GeometrySmith(N, V, L, roughness); 

		vec3 numerator    = NDF * G * F;
		float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0)  + 0.0001;
		vec3 specular     = numerator / denominator;  

		vec3 kD = vec3(1.0) - F; //Energy Conservation (total light = refracted light + reflected light) F = kS
		kD *= 1.0 - metallic;	

		float NdotL = max(dot(N, L), 0.0);        
		Lo += (kD * albedo / PI + specular) * radiance * NdotL;
	}

	//vec3 ambient = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;

	//vec3 ambient = vec3(0.03) * albedo * ao;
	vec3 ambient = vec3(0.03) * albedo;
    vec3 color = ambient + Lo;
	// Gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));  

    outColor = vec4(color, 1.0);
	//outColor = vec4(Lo,1.0);
	//outColor = vec4(1.0);
}