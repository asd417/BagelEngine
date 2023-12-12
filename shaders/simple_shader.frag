#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_KHR_vulkan_glsl : enable

//layout(location=0) in vec3 fragColor;
layout(location=0) in vec3 fragPosWorld;
layout(location=1) in vec3 fragNormalWorld;
layout(location=2) in vec2 fragUV;
layout(location=3) flat in int fragIndex;


layout(location=0) out vec4 outColor;

struct PointLight {
	vec4 position; // ignore w
	vec4 color; // w intensity
};

const float gamma = 2.2;
const float PI = 3.1415926538;
const int MAX_LIGHTS = 10; //Must match value in bagel_frame_info.hpp

layout(set = 0, binding = 0) uniform GlobalUBO {
	mat4 projectionMatrix;
	mat4 viewMatrix;
	mat4 inverseViewMatrix;
	vec4 ambientLightColor;
	PointLight pointLights[MAX_LIGHTS]; //Can use 'specialization constants' to set the size of this array at pipeline creation
	int numLights;
} ubo;

struct ObjectData{
	mat4 modelMatrix;
	mat4 normalMatrix;
};
layout (set = 0, binding = 1) readonly buffer objTransform {
	ObjectData objects[];
};

layout (set = 0, binding = 2) uniform sampler2D samplerColor[];
//layout (set = 1, binding = 0) uniform sampler2D samplerModelColor;

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	mat4 normalMatrix;
	uint textureHandle;
	bool useInstancedTransform;
} push;

//Normal distribution function
//Describes how much of the microfacets are aligned to the half angle vector, influenced by roughness
//a = roughness
//h = halfway vector 
//n = surface normal
float DistributionGGX(vec3 N, vec3 H, float a)
{
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float nom    = a2;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    denom        = PI * denom * denom;
	
    return nom / denom;
}


void main(){
	vec3 diffuseLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
	vec3 specularLight = vec3(0.0f);
	vec3 surfaceNormal = normalize(fragNormalWorld);

	vec3 cameraPosWorld = ubo.inverseViewMatrix[3].xyz;
	vec3 viewDir = normalize(cameraPosWorld - fragPosWorld);

	for(int i = 0;i<ubo.numLights;i++){
		PointLight light = ubo.pointLights[i];
		vec3 lightDir = light.position.xyz - fragPosWorld;
		float attenuation = 1.0 / dot(lightDir, lightDir);
		lightDir = normalize(lightDir);

		// diffuse light calculation
		float cosAngIncidence = max(dot(surfaceNormal,lightDir),0);
		vec3 intensity = light.color.xyz * light.color.w * attenuation;
		diffuseLight += intensity * cosAngIncidence;

		// specular light calculation
		vec3 halfwayDir = normalize(lightDir + viewDir);
		float blinnTerm = dot(surfaceNormal, halfwayDir);
		blinnTerm = clamp(blinnTerm,0,1);
		blinnTerm = pow(blinnTerm, 10); //Higher power = sharper highlight;
		specularLight += intensity * blinnTerm;
	}

	vec3 ambientLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
	vec4 UVcolor = texture(samplerColor[push.textureHandle], vec2(fragUV.x,fragUV.y), 1.0f);
	//vec4 UVcolor = vec4(1.0f,1.0f,1.0f,1.0f);
	// move colorspace of SRGB texture to linear space
	// This is okay because in BGLtexture class when we import the ktx textures, we assumed that it is in VK_FORMAT_R8G8B8A8_SRGB
	vec3 DiffuseColor = pow(UVcolor.rgb, vec3(gamma));

	outColor = vec4((diffuseLight + ambientLight) * DiffuseColor + specularLight * DiffuseColor,1.f);

	// move colorspace back to SRGB
    outColor.xyz = pow(outColor.xyz, vec3(1.0/gamma));
	//outColor = vec4(1.0f,1.0f,1.0f,1.0f);
}