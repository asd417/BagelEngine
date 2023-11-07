#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in vec3 fragColor;
layout(location=1) in vec3 fragPosWorld;
layout(location=2) in vec3 fragNormalWorld;
layout(location=3) in vec2 fragUV;
layout(location=4) flat in int fragIndex;

layout(location=0) out vec4 outColor;

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
	int numLights;
} ubo;
layout (binding = 1) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push { 
	mat4 modelMatrix;
	mat4 normalMatrix;
} push;

void main(){
	vec3 diffuseLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
	vec3 specularLight = vec3(0.0f);
	vec3 surfaceNormal = normalize(fragNormalWorld);

	vec3 cameraPosWorld = ubo.inverseViewMatrix[3].xyz;
	vec3 viewDirection = normalize(cameraPosWorld - fragPosWorld);

	for(int i = 0;i<ubo.numLights;i++){
		PointLight light = ubo.pointLights[i];
		vec3 directionToLight = light.position.xyz - fragPosWorld;
		float attenuation = 1.0 / dot(directionToLight, directionToLight);
		directionToLight = normalize(directionToLight);

		// diffuse light calculation
		float cosAngIncidence = max(dot(surfaceNormal,directionToLight),0);
		vec3 intensity = light.color.xyz * light.color.w * attenuation;
		diffuseLight += intensity * cosAngIncidence;

		// specular light calculation
		vec3 halfAngle = normalize(directionToLight + viewDirection);
		float blinnTerm = dot(surfaceNormal, halfAngle);
		blinnTerm = clamp(blinnTerm,0,1);
		blinnTerm = pow(blinnTerm, 512); //Higher power = sharper highlight;
		specularLight += intensity * blinnTerm;
	}

	vec3 ambientLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
	vec4 UVcolor = texture(samplerColor[fragIndex], vec2(fragUV.x,fragUV.y), 1.0f);
	outColor = UVcolor * vec4((diffuseLight + ambientLight) * fragColor + specularLight * fragColor,1.f);
}