// Shared Data transform code
#ifndef DATA_GLSL
#define DATA_GLSL

mat3 getTBN(vec3 fragTangent, vec3 fragBitangent, vec3 normal)
{
    vec3 T = normalize(fragTangent);
	vec3 B = normalize(fragBitangent);
	return mat3(T, B, normal);
}

#endif