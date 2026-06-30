#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
// #include <math.h>
namespace bagel
{
	// Uses Quaternions to calculate look vector pointing from origin to lookTarget
	// up is {0,1,0} by default
	// alternateUp is used when lookTarget - pos is almost parallel to up vector. {1,0,0} by default
	inline glm::vec3 GetLookVector(glm::vec3 origin, glm::vec3 lookTarget, glm::vec3 up = {0, 1, 0}, glm::vec3 alternateUp = {1, 0, 0})
	{
		glm::vec3 dirVec = glm::normalize(lookTarget - origin);

		glm::qua<float> qrot;
		if (glm::abs(glm::dot(dirVec, up)) > .9999f)
			qrot = glm::quatLookAt(-dirVec, alternateUp);
		else
			qrot = glm::quatLookAt(-dirVec, up);
		// OpenGL's forward vector is -z
		qrot.z = qrot.z * -1;
		return glm::eulerAngles(qrot);
	}

	inline float perlin(const glm::vec3 &p, uint32_t seed = 0)
	{
		// Quintic interpolation curve 6t^5-15t^4+10t^3 (zero 1st/2nd derivative at ends).
		auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };

		// Integer hash -> gradient. Picks one of 12 edge-of-cube gradient directions.
		// seed shifts the whole field to a different, independent realisation.
		auto gradDot = [seed](int ix, int iy, int iz, float x, float y, float z)
		{
			uint32_t h = uint32_t(ix) * 0x8DA6B343u ^ uint32_t(iy) * 0xD8163841u ^ uint32_t(iz) * 0xCB1AB31Fu ^ seed * 0x9E3779B1u;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12; // avalanche
			switch (h & 15)
			{ // Ken Perlin's 12 gradients (+4 dupes)
			case 0:
				return x + y;
			case 1:
				return -x + y;
			case 2:
				return x - y;
			case 3:
				return -x - y;
			case 4:
				return x + z;
			case 5:
				return -x + z;
			case 6:
				return x - z;
			case 7:
				return -x - z;
			case 8:
				return y + z;
			case 9:
				return -y + z;
			case 10:
				return y - z;
			case 11:
				return -y - z;
			case 12:
				return x + y;
			case 13:
				return -y + z;
			case 14:
				return -x + y;
			default:
				return -y - z;
			}
		};

		glm::vec3 pf = glm::floor(p);
		glm::ivec3 i = glm::ivec3(pf); // unit cube corner
		glm::vec3 f = p - pf;		   // fractional position in cube [0,1)
		glm::vec3 u = {fade(f.x), fade(f.y), fade(f.z)};

		// dot of gradient at each of 8 corners with the vector to p
		float n000 = gradDot(i.x, i.y, i.z, f.x, f.y, f.z);
		float n100 = gradDot(i.x + 1, i.y, i.z, f.x - 1.0f, f.y, f.z);
		float n010 = gradDot(i.x, i.y + 1, i.z, f.x, f.y - 1.0f, f.z);
		float n110 = gradDot(i.x + 1, i.y + 1, i.z, f.x - 1.0f, f.y - 1.0f, f.z);
		float n001 = gradDot(i.x, i.y, i.z + 1, f.x, f.y, f.z - 1.0f);
		float n101 = gradDot(i.x + 1, i.y, i.z + 1, f.x - 1.0f, f.y, f.z - 1.0f);
		float n011 = gradDot(i.x, i.y + 1, i.z + 1, f.x, f.y - 1.0f, f.z - 1.0f);
		float n111 = gradDot(i.x + 1, i.y + 1, i.z + 1, f.x - 1.0f, f.y - 1.0f, f.z - 1.0f);

		// trilinear blend
		float nx00 = glm::mix(n000, n100, u.x), nx10 = glm::mix(n010, n110, u.x);
		float nx01 = glm::mix(n001, n101, u.x), nx11 = glm::mix(n011, n111, u.x);
		float nxy0 = glm::mix(nx00, nx10, u.y), nxy1 = glm::mix(nx01, nx11, u.y);
		return glm::mix(nxy0, nxy1, u.z); // ~[-1, 1]
	}

	// Perlin value + analytic gradient (mirrors noise.glsl::perlinD). Returns
	// vec4(value, d/dp). Only the gradient is consumed (terrain normals); the value
	// path is NOT on the height chain (that stays perlin() above), so heights are
	// untouched. The 16 gradient vectors equal perlin()'s switch cases one-for-one.
	inline glm::vec4 perlinD(const glm::vec3 &p, uint32_t seed = 0)
	{
		static const glm::vec3 GRAD[16] = {
			{ 1, 1, 0}, {-1, 1, 0}, { 1,-1, 0}, {-1,-1, 0},
			{ 1, 0, 1}, {-1, 0, 1}, { 1, 0,-1}, {-1, 0,-1},
			{ 0, 1, 1}, { 0,-1, 1}, { 0, 1,-1}, { 0,-1,-1},
			{ 1, 1, 0}, { 0,-1, 1}, {-1, 1, 0}, { 0,-1,-1}
		};
		auto fade  = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
		auto fadeD = [](float t) { return 30.0f * t * t * (t * (t - 2.0f) + 1.0f); };
		auto gradVec = [seed](int ix, int iy, int iz) -> glm::vec3 {
			uint32_t h = uint32_t(ix) * 0x8DA6B343u ^ uint32_t(iy) * 0xD8163841u ^ uint32_t(iz) * 0xCB1AB31Fu ^ seed * 0x9E3779B1u;
			h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
			return GRAD[h & 15];
		};

		glm::vec3 pf = glm::floor(p);
		glm::ivec3 i = glm::ivec3(pf);
		glm::vec3 f = p - pf;
		glm::vec3 u  = { fade(f.x),  fade(f.y),  fade(f.z)  };
		glm::vec3 du = { fadeD(f.x), fadeD(f.y), fadeD(f.z) };

		glm::vec3 g[8]; float n[8];
		for (int c = 0; c < 8; ++c)
		{
			glm::ivec3 o = { c & 1, (c >> 1) & 1, (c >> 2) & 1 };
			g[c] = gradVec(i.x + o.x, i.y + o.y, i.z + o.z);
			n[c] = glm::dot(g[c], f - glm::vec3(o));
		}
		float nx00 = glm::mix(n[0], n[1], u.x), nx10 = glm::mix(n[2], n[3], u.x);
		float nx01 = glm::mix(n[4], n[5], u.x), nx11 = glm::mix(n[6], n[7], u.x);
		float nxy0 = glm::mix(nx00, nx10, u.y), nxy1 = glm::mix(nx01, nx11, u.y);
		float val  = glm::mix(nxy0, nxy1, u.z);

		glm::vec3 grad(0.0f);
		for (int c = 0; c < 8; ++c)
		{
			glm::vec3 a = { float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1) };
			glm::vec3 W = glm::mix(1.0f - u, u, a);
			glm::vec3 s = glm::mix(glm::vec3(-1.0f), glm::vec3(1.0f), a) * du;
			grad += n[c] * glm::vec3(s.x * W.y * W.z, W.x * s.y * W.z, W.x * W.y * s.z);
			grad += (W.x * W.y * W.z) * g[c];
		}
		return glm::vec4(val, grad.x, grad.y, grad.z);
	}
	//simplex noise

	
}