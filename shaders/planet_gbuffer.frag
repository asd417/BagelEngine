#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require
#include "noise.glsl"
#include "data_transform.glsl"   // octEncode

// Planet gbuffer fill (step 2): reconstructs the surface per-fragment from the SAME
// fbm that displaces the mesh (noise.glsl), evaluated at the LOD-invariant radial
// direction. Height + an analytic normal (from the fbm's tangential gradient) are
// derived here, so shading detail is independent of the triangle tessellation and
// can run finer than the mesh. Albedo is a placeholder height tint (step 3 = biomes).

layout(location=0) in vec3 fragPosWorld;
layout(location=1) in vec3 fragNormalWorld;

layout(location=0) out vec4 outNormal;   // rg = oct-encoded normal, b = roughness, a = metallic
layout(location=1) out vec4 outAlbedo;   // xyz = albedo, w = 1.0 (valid pixel)
layout(location=2) out vec4 outEmission; // xyz = emission, w = unused

layout(set=0,binding=6) uniform sampler2D samplerColor[]; // bindless array (shared with gbuffer_fill)

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    vec4 centerRadius; // xyz = planet center (world), w = base radius
    vec4 noiseF;       // amplitude, frequency, lacunarity, gain
    vec4 noiseF2;      // sealevel, octaves, seed, gradient-epsilon
} push;

void main() {
    vec3 d = normalize(fragPosWorld - push.centerRadius.xyz); // LOD-invariant direction

    // Displaced-surface normal of the height field P=H(d)*d. The fbm value AND its analytic
    // gradient come from a SINGLE fbmD eval (was 3 finite-difference fbm evals).
    // Matches planet_terrain.hpp::surfaceNormal / baseHeightGrad.
    vec4 nd = fbmD(d, push.noiseF.y, int(push.noiseF2.y), push.noiseF.z, push.noiseF.w,
                   uint(push.noiseF2.z)); // .x = fbm value, .yzw = d(value)/ddir
    // No sea-level clamp (matches planet_terrain.hpp::baseHeight): the ocean is a separate
    // transparent sphere, so terrain keeps its full noise band and dips below the sea radius.
    float H0 = push.centerRadius.w + push.noiseF.x * nd.x;
    // n = normalize(H0*d - gTang), gTang = tangent-plane component of the height gradient.
    // Using the projection directly (not an arbitrary T/B basis) keeps the normal continuous
    // everywhere — including the poles, where a tangent frame would flip and leave a seam ring.
    vec3 gradH = push.noiseF.x * nd.yzw;
    vec3 gTang = gradH - dot(gradH, d) * d;
    vec3 normal = normalize(H0 * d - gTang); // outward (H0 > 0)

    // Placeholder albedo: tint across the natural height band so reconstruction is visible.
    float lo = push.centerRadius.w - push.noiseF.x;
    float hN = clamp((H0 - lo) / (2.0 * push.noiseF.x), 0.0, 1.0);
    vec3 albedo = mix(vec3(0.18, 0.42, 0.18), vec3(0.92, 0.92, 0.95), hN);

    outNormal   = vec4(octEncode(normal), 0.9, 0.0);
    outAlbedo   = vec4(albedo, 1.0);
    outEmission = vec4(0.0);
}
