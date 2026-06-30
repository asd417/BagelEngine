#version 450
#extension GL_EXT_nonuniform_qualifier:enable
#extension GL_GOOGLE_include_directive:require
#include "pbr.glsl"
#include "noise.glsl"   // perlin4D for the animated ocean waves

// Procedural ocean/water pass. Runs AFTER the transparent pass, in the same HDR radiosity pass
// (depth-test read-only against the opaque G-buffer depth, no depth write), alpha-blended in.
// Split out of transparent.frag so water can become depth-aware (see TODO below).

layout(location=0)in vec3 fragPosWorld;
layout(location=1)in vec3 fragNormalWorld;

// Single output: linear HDR, alpha-blended into the radiosity buffer (composite tonemaps later).
layout(location=0)out vec4 outColor;

layout(set=0,binding=6)uniform sampler2D samplerColor[];
// Directional shadow maps (one per cascade) — same binding/layout as the deferred/transparent pass.
layout(set=0,binding=7)uniform sampler2DShadow shadowMaps[CASCADE_COUNT];

// Depth-aware opacity. We do NOT sample the radiosity color here (it's this pass's write target —
// sampling it would be a feedback loop); the alpha-over blend already composites the water over the
// terrain that's behind it in the radiosity buffer. We only need the opaque scene DEPTH (gDepth, a
// read-only depth attachment in this pass, sampled via the bindless table) to:
//   1. Fix "water over outer space": where there's no opaque geometry behind the limb the depth is
//      the far plane -> treat the water as fully opaque deep water (alpha = 1) instead of letting
//      space show through the semi-transparent surface.
//   2. Fade by thickness: reconstruct the sea-floor world pos from gDepth and measure how much water
//      sits between the surface and the floor. Shallow water stays clear (terrain shows through the
//      blend); deep water firms up toward opaque. Grazing angles stay opaque via the Fresnel term.

// PCF via hardware compare. 1.0 = lit, 0.0 = shadowed. (mirrors transparent.frag / radiosity.frag)
float sampleShadow(vec2 uv, int cascade, float refDepth) {
	const int filterSize = 5;
	vec2 texelSize = 1.0 / vec2(textureSize(shadowMaps[nonuniformEXT(cascade)], 0));
	float shadow = 0.0;
	for (int x = -(filterSize-1)/2; x <= (filterSize-1)/2; ++x)
		for (int y = -(filterSize-1)/2; y <= (filterSize-1)/2; ++y)
			shadow += texture(shadowMaps[nonuniformEXT(cascade)], vec3(uv + vec2(x,y)*texelSize, refDepth));
	return shadow / float(filterSize*filterSize);
}

// Must match the push block in water.vert (and WaterPushConstantData on the C++ side).
layout(push_constant)uniform Push{
	mat4 modelMatrix;
	vec4 scale;
	float time;          // cumulative seconds (animated ocean waves)
	uint gDepthHandle;   // bindless slot of the opaque G-buffer depth (scene depth behind the water)
	float opaqueDepth;   // water column (world units) that reads opaque at camRefDist (live from ImGui)
	float camRefDist;    // reference camera distance for the depth->opacity scaling (live from ImGui)
}push;

const float minWaterAlpha = 0.2f;
// Procedural ocean surface: animated wave normal (single octave of 4D perlin, w=time), deep-blue
// tint, and a Fresnel-boosted alpha so the water firms up at grazing angles. Extracted unchanged
// from transparent.frag::shadeOcean; the depth-based refraction in the TODO above replaces this.
void shadeOcean(vec3 fragPos, vec3 V, float t,
                out vec3 albedo, out vec3 normal, out float roughness, out float metallic, out float alpha) {
	vec3 N = normalize(fragNormalWorld);
	const float freq = 0.2, speed = 0.35, amp = 0.2;
	vec4 wp = vec4(fragPos * freq, t * speed);
	// Analytic wave gradient from ONE perlin4D; perturb N by the tangent-plane component (basis-free,
	// so the wave normal is continuous everywhere including the poles).
	vec3 gW    = freq * perlin4D(wp, 1u).yzw;
	vec3 gTang = gW - dot(gW, N) * N;
	normal = normalize(N - amp * gTang);
	albedo = vec3(0.0235, 0.1059, 0.2);   // deep blue (linear)
	roughness = 0.06; metallic = 0.0;
	float fres = pow(1.0 - max(dot(normal, V), 0.0), 5.0);
	alpha = clamp(0.55 + 0.45 * fres, 0.0, 1.0); // ~55% face-on, opaque at grazing angles
}

void main(){
	vec3 camPos = ubo.inverseViewMatrix[3].xyz;
	vec3 V = normalize(camPos - fragPosWorld);

	vec3  albedo, normal;
	float roughness, metallic, alpha;
	shadeOcean(fragPosWorld, V, push.time, albedo, normal, roughness, metallic, alpha);

	vec3 F0 = mix(vec3(.04), albedo, metallic);
	vec3 Lo = vec3(0.);

	// Point lights
	for(int i=0;i<int(ubo.numLights);i++){
		Lo += calculatePointLight(ubo.pointLights[i], fragPosWorld, ubo.exposure, normal, V, albedo, F0, roughness, metallic);
	}

	// Directional light + cascaded shadows (mirrors transparent.frag).
	if(ubo.hasDirLight!=0){
		vec3 L = normalize(-ubo.directionalLight.direction.xyz);
		vec3 radiance = ubo.directionalLight.color.xyz * ubo.directionalLight.color.w * ubo.exposure;

		float viewDepth = -(ubo.viewMatrix * vec4(fragPosWorld,1.0)).z;
		int cascade = CASCADE_COUNT-1;
		if     (viewDepth<ubo.directionalLight.cascadeSplits.x) cascade=0;
		else if(viewDepth<ubo.directionalLight.cascadeSplits.y) cascade=1;
		else if(viewDepth<ubo.directionalLight.cascadeSplits.z) cascade=2;

		float shadowFactor=1.0;
		if(viewDepth<ubo.directionalLight.cascadeSplits.w){
			vec4 lsPos = ubo.directionalLight.lightSpaceMatrix[cascade] * vec4(fragPosWorld,1.0);
			vec2 shadowUV = lsPos.xy*0.5+0.5;
			if(shadowUV.x>0.0&&shadowUV.x<1.0&&shadowUV.y>0.0&&shadowUV.y<1.0&&lsPos.z>0.0&&lsPos.z<1.0){
				float bias = max(ubo.shadowBiasSlope*(1.0-dot(normal,L)), ubo.shadowBiasMin);
				shadowFactor = sampleShadow(shadowUV, cascade, lsPos.z-bias);
			}
		}
		Lo += shadowFactor * pbrDirectLight(normal, V, L, radiance, albedo, F0, roughness, metallic);
	}

	vec3 ambient = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w * albedo;
	vec3 hdr = ambient + Lo;

	// --- Depth-aware opacity -------------------------------------------------------------------
	// Sample the opaque scene depth at this same pixel and reconstruct the sea-floor world position,
	// then measure the water column thickness between the surface (fragPosWorld) and the floor. Both
	// points lie on the same view ray (same pixel), so their distance is the depth of water seen here.
	ivec2 depthSize = textureSize(samplerColor[nonuniformEXT(push.gDepthHandle)], 0);
	vec2  screenUV  = gl_FragCoord.xy / vec2(depthSize);
	float sceneDepth = texture(samplerColor[nonuniformEXT(push.gDepthHandle)], screenUV).r;
	// Reconstruct world pos from depth (matches radiosity.frag: xy in NDC, z is 0..1 depth directly).
	vec4 clip  = vec4(screenUV * 2.0 - 1.0, sceneDepth, 1.0);
	vec4 world = ubo.invViewProjMatrix * clip;
	vec3 sceneWorldPos = world.xyz / world.w;
	float thickness = length(sceneWorldPos - fragPosWorld); // surface -> sea floor, along the view ray

	// Screen-relative opacity: scale the opaque-depth threshold by how far the surface is from the
	// camera so the falloff is in screen space, not a fixed world distance. Up close the threshold is
	// large -> the same water column reads as more transparent and you see deeper into it; far away it
	// shrinks -> the surface firms up to opaque. (A literal screen-space DEPTH difference would invert
	// this — perspective makes a fixed world gap project larger up close — so we scale by distance.)
	float camDist     = length(camPos - fragPosWorld);
	float opaqueDepth = push.opaqueDepth * (push.camRefDist / max(camDist, 1.0));
	float depthAlpha  = clamp(thickness / opaqueDepth, 0.0, 1.0);
	// Grazing angles read as opaque regardless of depth (Fresnel); face-on lets the depth term decide.
	float fres = pow(1.0 - max(dot(normal, V), 0.0), 5.0);
	alpha = clamp(mix(depthAlpha, 1.0, fres), minWaterAlpha, 1.0);

	// Blend linear HDR into the radiosity buffer (alpha-over). Composite tonemaps; bloom thresholds.
	outColor = vec4(hdr, alpha);
	//outColor = vec4(vec3(depthAlpha), 1.0); return;
}
