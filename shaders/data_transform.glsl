// Shared Data transform code
#ifndef DATA_GLSL
#define DATA_GLSL

mat3 getTBN(vec3 fragTangent, vec3 fragBitangent, vec3 normal)
{
    vec3 T = normalize(fragTangent);
	vec3 B = normalize(fragBitangent);
	return mat3(T, B, normal);
}

// Octahedral normal encoding (unit vec3 -> [0,1]^2). Pairs with pbr.glsl::octDecode.
vec2 octEncode(vec3 n) {
	float l1 = abs(n.x) + abs(n.y) + abs(n.z);
	vec2 p = n.xy / l1;
	if (n.z < 0.0) p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
	return p * 0.5 + 0.5;
}

#endif