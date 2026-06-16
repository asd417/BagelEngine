// Shared Cook-Torrance PBR/BRDF helpers.
// Included at compile time by both the deferred opaque lighting pass (radiosity.frag)
// and the forward transparent pass (transparent.frag) so the lighting math stays identical.
// The including shader must declare a #version before this file is included.
#ifndef PBR_GLSL
#define PBR_GLSL
// Light structs, constants, and the GlobalUBO block live in ubo.glsl now.
#include "ubo.glsl"

const float PBR_PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a2    = roughness*roughness*roughness*roughness;
    float NdotH = max(dot(N,H), 0.0);
    float denom = (NdotH*NdotH*(a2-1.0)+1.0);
    return a2 / (PBR_PI*denom*denom);
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

// Outgoing radiance contribution from a single light direction L (already normalized),
// where `radiance` is the incoming light radiance at the surface (color * intensity * attenuation).
vec3 pbrDirectLight(vec3 N, vec3 V, vec3 L, vec3 radiance,
                    vec3 albedo, vec3 F0, float roughness, float metallic) {
    vec3  H   = normalize(V + L);
    vec3  F   = FresnelSchlick(max(dot(H,V),0.0), F0);
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  sp  = (NDF*G*F) / (4.0*max(dot(N,V),0.0)*max(dot(N,L),0.0) + 0.0001);
    vec3  kD  = (vec3(1.0)-F)*(1.0-metallic);
    return (kD*albedo/PBR_PI + sp) * radiance * max(dot(N,L), 0.0);
}

vec3 calculatePointLight(PointLight pl, vec3 fragPosWorld, float exposure, vec3 normal, vec3 V, vec3 albedo, vec3 F0, float roughness, float metallic)
{
    vec3 toLight   = pl.position.xyz - fragPosWorld;
    float atten    = 1.0 / dot(toLight, toLight);
    vec3  L        = normalize(toLight);
    vec3  radiance = pl.color.xyz * pl.color.w * exposure * atten;
    return pbrDirectLight(normal, V, L, radiance, albedo, F0, roughness, metallic);
}

vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec3 octDecode(vec2 p) {
    p = p * 2.0 - 1.0;
    vec3 n = vec3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * signNotZero(n.xy);
    return normalize(n);
}

#endif // PBR_GLSL
