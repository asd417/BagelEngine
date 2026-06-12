#version 450
#extension GL_EXT_nonuniform_qualifier:enable
#extension GL_KHR_vulkan_glsl:enable
#extension GL_GOOGLE_include_directive:require
#include "pbr.glsl"
#include "data_transform.glsl"

struct VS_OUT{
	int isInstancedTransform;
	uint albedoMap;
	uint normalMap;
	uint metalRoughMap;
	uint emissionMap;
};

layout(location=0)in vec3 fragPosWorld;
layout(location=1)in vec2 fragUV;
layout(location=2)in vec3 fragTangent;
layout(location=3)in vec3 fragBitangent;
layout(location=4)in vec3 fragNormalWorld;
layout(location=5)flat in VS_OUT fs_in;

// Single output: linear HDR blended into the radiosity buffer (composite tonemaps later).
layout(location=0)out vec4 outColor;

layout(set=0,binding=4)uniform GlobalUBO{
	mat4 projectionMatrix;
	mat4 viewMatrix;
	mat4 inverseViewMatrix;
	vec4 ambientLightColor;
	PointLight pointLights[MAX_LIGHTS];
	uint numLights;
	// std140 auto-aligns vec4 lineColor to 544
	vec4 lineColor;
	mat4 invViewProjMatrix;
	float exposure;
	// std140 auto-aligns DirectionalLight to 640
	DirectionalLight directionalLight;
	uint hasDirLight;
	uint shadowMapHandle;
	float shadowBiasMin;
	float shadowBiasSlope;
}ubo;

layout(set=0,binding=6)uniform sampler2D samplerColor[];

layout(push_constant)uniform Push{
	mat4 modelMatrix;
	vec4 scale;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
	float emissionLux;
}push;

void main(){
	// technically since submeshes gets flagged as transparent
	// when their albedo map exists and when it has transparency,
	// there will always be an albedo map to sample from.
	vec4 albedoSample=texture(samplerColor[fs_in.albedoMap],fragUV);
	vec3 albedo=albedoSample.rgb;
	float alpha=albedoSample.a;
	
	//same as gbuffer fill
	vec3 normal=normalize(fragNormalWorld);
	if(fs_in.normalMap!=0){
		mat3 TBN=getTBN(fragTangent,fragBitangent,normal);
		vec3 n=texture(samplerColor[fs_in.normalMap],fragUV).rgb*2.-1.;
		normal=normalize(TBN*n);
	}
	float metallic=0.;
	float roughness=.5;
	// glTF ORM: R=occlusion, G=roughness, B=metallic
	if(fs_in.metalRoughMap!=0){
		vec3 mr=texture(samplerColor[fs_in.metalRoughMap],fragUV).rgb;
		roughness=clamp(mr.g,0.,1.);
		metallic=clamp(mr.b,0.,1.);
	}
	// this is slightly different from gbuffer_fill approach.
	// not sure why the gbuffer_fill uses push constant for emission map but not this shader
	vec4 emData=vec4(0.);
	if(fs_in.emissionMap!=0)
	{
		vec3 emission=texture(samplerColor[fs_in.emissionMap],fragUV).rgb;
		emData=vec4(emission,clamp(push.emissionLux/10000.,0.,1.));
	}
	
	vec3 camPos=ubo.inverseViewMatrix[3].xyz;
	vec3 V=normalize(camPos-fragPosWorld);
	vec3 F0=mix(vec3(.04),albedo,metallic);
	vec3 Lo=vec3(0.);
	
	// Point lights
	for(int i=0;i<int(ubo.numLights);i++){
		Lo+=calculatePointLight(ubo.pointLights[i],fragPosWorld,ubo.exposure,normal,V,albedo,F0,roughness,metallic);
	}
	
	vec3 ambient=ubo.ambientLightColor.xyz*ubo.ambientLightColor.w*albedo;
	vec3 emission=emData.rgb*emData.a*10000.*ubo.exposure;
	vec3 hdr=ambient+Lo+emission;

	// Blend linear HDR into the radiosity buffer (alpha-over). Composite tonemaps; bloom thresholds.
	outColor=vec4(hdr,alpha);
}
