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

// TODO(depth-water): this pass currently just alpha-blends the ocean over whatever is already in
// the radiosity buffer. Two consequences to fix by going depth-aware:
//   1. Over the bare "outer space" background the water reads as semi-transparent (the planet's
//      limb shows space) instead of opaque deep water.
//   2. Under-sea terrain detail isn't visible through the surface.
// Fix: sample the opaque scene DEPTH (gDepth) and the radiosity COLOR for this fragment. Compute
// waterThickness = linearize(sceneDepth) - linearize(surfaceDepth); tint by thickness (deep =
// opaque blue); Fresnel-blend between the refracted under-sea color (face-on / shallow) and the
// opaque surface (grazing / deep / over space, where sceneDepth = far plane -> fully opaque).
// Requires binding gDepth + the radiosity color as sampled textures available to this pass.

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
}push;

// Procedural ocean surface: animated wave normal (single octave of 4D perlin, w=time), deep-blue
// tint, and a Fresnel-boosted alpha so the water firms up at grazing angles. Extracted unchanged
// from transparent.frag::shadeOcean; the depth-based refraction in the TODO above replaces this.
void shadeOcean(vec3 fragPos, vec3 V, float t,
                out vec3 albedo, out vec3 normal, out float roughness, out float metallic, out float alpha) {
	vec3 N = normalize(fragNormalWorld);
	const float freq = 0.18, speed = 0.35, amp = 0.5;
	vec4 wp = vec4(fragPos * freq, t * speed);
	// Analytic wave gradient from ONE perlin4D; perturb N by the tangent-plane component (basis-free,
	// so the wave normal is continuous everywhere including the poles).
	vec3 gW    = freq * perlin4D(wp, 1u).yzw;
	vec3 gTang = gW - dot(gW, N) * N;
	normal = normalize(N - amp * gTang);
	albedo = vec3(0.02, 0.16, 0.32);   // deep blue (linear)
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

	// Blend linear HDR into the radiosity buffer (alpha-over). Composite tonemaps; bloom thresholds.
	outColor = vec4(hdr, alpha);
}
