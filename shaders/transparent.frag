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

// GlobalUBO (binding 4) comes from ubo.glsl via pbr.glsl.

layout(set=0,binding=6)uniform sampler2D samplerColor[];

// Directional shadow maps (one per cascade) — same binding/layout as the deferred pass.
layout(set=0,binding=7)uniform sampler2DShadow shadowMaps[CASCADE_COUNT];

// PCF via hardware compare. 1.0 = lit, 0.0 = shadowed. (mirrors radiosity.frag)
float sampleShadow(vec2 uv, int cascade, float refDepth) {
	const int filterSize = 5;
	vec2 texelSize = 1.0 / vec2(textureSize(shadowMaps[nonuniformEXT(cascade)], 0));
	float shadow = 0.0;
	for (int x = -(filterSize-1)/2; x <= (filterSize-1)/2; ++x)
		for (int y = -(filterSize-1)/2; y <= (filterSize-1)/2; ++y)
			shadow += texture(shadowMaps[nonuniformEXT(cascade)], vec3(uv + vec2(x,y)*texelSize, refDepth));
	return shadow / float(filterSize*filterSize);
}

layout(push_constant)uniform Push{
	mat4 modelMatrix;
	vec4 scale;
	uint BufferedTransformHandle;
	uint UsesBufferedTransform;
	uint materialRowBase;
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

	// Directional light + cascaded shadows (mirrors radiosity.frag, so transparent surfaces
	// receive the sun + its shadows the same way opaque ones do).
	if(ubo.hasDirLight!=0){
		vec3 L=normalize(-ubo.directionalLight.direction.xyz);
		vec3 radiance=ubo.directionalLight.color.xyz*ubo.directionalLight.color.w*ubo.exposure;

		float viewDepth=-(ubo.viewMatrix*vec4(fragPosWorld,1.0)).z;
		int cascade=CASCADE_COUNT-1;
		if     (viewDepth<ubo.directionalLight.cascadeSplits.x) cascade=0;
		else if(viewDepth<ubo.directionalLight.cascadeSplits.y) cascade=1;
		else if(viewDepth<ubo.directionalLight.cascadeSplits.z) cascade=2;

		float shadowFactor=1.0;
		if(viewDepth<ubo.directionalLight.cascadeSplits.w){
			vec4 lsPos=ubo.directionalLight.lightSpaceMatrix[cascade]*vec4(fragPosWorld,1.0);
			vec2 shadowUV=lsPos.xy*0.5+0.5;
			if(shadowUV.x>0.0&&shadowUV.x<1.0&&shadowUV.y>0.0&&shadowUV.y<1.0&&lsPos.z>0.0&&lsPos.z<1.0){
				float bias=max(ubo.shadowBiasSlope*(1.0-dot(normal,L)),ubo.shadowBiasMin);
				shadowFactor=sampleShadow(shadowUV,cascade,lsPos.z-bias);
			}
		}
		Lo+=shadowFactor*pbrDirectLight(normal,V,L,radiance,albedo,F0,roughness,metallic);
	}

	vec3 ambient=ubo.ambientLightColor.xyz*ubo.ambientLightColor.w*albedo;
	vec3 emission=emData.rgb*emData.a*10000.*ubo.exposure;
	vec3 hdr=ambient+Lo+emission;

	// Blend linear HDR into the radiosity buffer (alpha-over). Composite tonemaps; bloom thresholds.
	outColor=vec4(hdr,alpha);
}
