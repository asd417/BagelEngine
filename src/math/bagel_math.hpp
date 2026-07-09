#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
// #include <math.h>
namespace bagel
{
	struct Ray
	{
		glm::vec3 origin;
		glm::vec3 direction;
		double length = 999999.0f; // max length by default
	};

	// ---- basis / rotation extraction ---------------------------------------

	// World direction of axis i (0/1/2 = X/Y/Z).
	inline glm::vec3 axisVec(int i)
	{
		return (i == 0) ? glm::vec3(1, 0, 0) : (i == 1) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
	}

	// Basis columns of `m` with their scale stripped. Identity if any column is degenerate.
	// Assumes no shear and a right-handed basis (a mirrored matrix stays mirrored).
	inline glm::mat3 orthoBasis(const glm::mat4 &m)
	{
		const glm::vec3 x = glm::vec3(m[0]), y = glm::vec3(m[1]), z = glm::vec3(m[2]);
		const float lx = glm::length(x), ly = glm::length(y), lz = glm::length(z);
		if (lx < 1e-6f || ly < 1e-6f || lz < 1e-6f) return glm::mat3{1.0f};
		return glm::mat3{x / lx, y / ly, z / lz};
	}

	// Rotation of a world matrix whose columns may carry scale.
	//
	// glm::quat_cast REQUIRES an orthonormal input: it recovers the quaternion via
	// sqrt(biggestSquaredMinus1 + 1), and that +1 is NOT scaled along with the matrix. Feed it
	// s*R and the result is not even a scalar multiple of the true quaternion, so normalizing
	// afterwards cannot recover it. At s = 0.1 (TransformComponent's default scale) a measured
	// 90 deg rotation about Z reads back as 159.4 deg, and 120 deg about (1,1,1) as 19.7 deg --
	// the error is neither small nor monotonic, since the scale can flip which component the
	// algorithm picks as largest. Always strip the scale first; that is what this does.
	inline glm::quat rotationOf(const glm::mat4 &m)
	{
		return glm::normalize(glm::quat_cast(orthoBasis(m)));
	}

	// ---- ray intersection ---------------------------------------------------

	// Closest approach between ray (o,d) and the infinite line through p along unit a.
	// Returns the line parameter s (signed world distance along a from p) and the two closest
	// points. False if near-parallel or the closest ray point is behind the camera.
	inline bool closestOnAxis(const glm::vec3 &o, const glm::vec3 &d, const glm::vec3 &p,
							  const glm::vec3 &a, float &s, glm::vec3 &axisPoint, glm::vec3 &rayPoint)
	{
		const glm::vec3 e = p - o;
		const float ad = glm::dot(a, d);
		const float dd = glm::dot(d, d);
		const float ae = glm::dot(a, e);
		const float de = glm::dot(d, e);
		const float denom = ad * ad - dd;
		if (glm::abs(denom) < 1e-6f) return false;
		const float t = (ae * ad - de) / denom;
		s = t * ad - ae;
		axisPoint = p + s * a;
		rayPoint = o + t * d;
		return t > 0.0f;
	}

	// Ray (o,d) against the plane through c with normal n. False if parallel or behind.
	inline bool rayPlane(const glm::vec3 &o, const glm::vec3 &d, const glm::vec3 &c,
						 const glm::vec3 &n, glm::vec3 &hit)
	{
		const float dn = glm::dot(d, n);
		if (glm::abs(dn) < 1e-6f) return false;
		const float t = glm::dot(c - o, n) / dn;
		if (t <= 0.0f) return false;
		hit = o + t * d;
		return true;
	}

	// Ray (o,d) against the sphere at c with radius r. Returns the nearest non-negative hit.
	inline bool raySphere(const glm::vec3 &o, const glm::vec3 &d, const glm::vec3 &c, float r, float &tHit)
	{
		const glm::vec3 oc = o - c;
		const float a = glm::dot(d, d);
		const float b = 2.0f * glm::dot(oc, d);
		const float cc = glm::dot(oc, oc) - r * r;
		const float disc = b * b - 4.0f * a * cc;
		if (disc < 0.0f) return false;
		const float sq = glm::sqrt(disc);
		float t = (-b - sq) / (2.0f * a);
		if (t < 0.0f) t = (-b + sq) / (2.0f * a);
		if (t < 0.0f) return false;
		tHit = t;
		return true;
	}

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

	// Kernels shared by perlin()/perlinD(). Cannot be named `perlin`: a namespace and a function
	// cannot share a name in the same scope, and `perlin` is already the function below.
	namespace perlinNS
	{
		// Quintic interpolation curve 6t^5-15t^4+10t^3 (zero 1st/2nd derivative at ends).
		inline float fade(float t)
		{
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}

		// Integer hash -> gradient. Picks one of 12 edge-of-cube gradient directions.
		// seed shifts the whole field to a different, independent realisation.
		// `inline` is required: this is a header, so a non-inline definition would collide at
		// link time across every TU that includes bagel_math.hpp.
		inline float gradDot(uint32_t seed, int ix, int iy, int iz, float x, float y, float z)
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
		}
	}

	inline float perlin(const glm::vec3 &p, uint32_t seed = 0)
	{
		glm::vec3 pf = glm::floor(p);
		glm::ivec3 i = glm::ivec3(pf); // unit cube corner
		glm::vec3 f = p - pf;		   // fractional position in cube [0,1)
		glm::vec3 u = {perlinNS::fade(f.x), perlinNS::fade(f.y), perlinNS::fade(f.z)};

		// dot of gradient at each of 8 corners with the vector to p
		float n000 = perlinNS::gradDot(seed,i.x, i.y, i.z, f.x, f.y, f.z);
		float n100 = perlinNS::gradDot(seed,i.x + 1, i.y, i.z, f.x - 1.0f, f.y, f.z);
		float n010 = perlinNS::gradDot(seed,i.x, i.y + 1, i.z, f.x, f.y - 1.0f, f.z);
		float n110 = perlinNS::gradDot(seed,i.x + 1, i.y + 1, i.z, f.x - 1.0f, f.y - 1.0f, f.z);
		float n001 = perlinNS::gradDot(seed,i.x, i.y, i.z + 1, f.x, f.y, f.z - 1.0f);
		float n101 = perlinNS::gradDot(seed,i.x + 1, i.y, i.z + 1, f.x - 1.0f, f.y, f.z - 1.0f);
		float n011 = perlinNS::gradDot(seed,i.x, i.y + 1, i.z + 1, f.x, f.y - 1.0f, f.z - 1.0f);
		float n111 = perlinNS::gradDot(seed,i.x + 1, i.y + 1, i.z + 1, f.x - 1.0f, f.y - 1.0f, f.z - 1.0f);

		// trilinear blend
		float nx00 = glm::mix(n000, n100, u.x), nx10 = glm::mix(n010, n110, u.x);
		float nx01 = glm::mix(n001, n101, u.x), nx11 = glm::mix(n011, n111, u.x);
		float nxy0 = glm::mix(nx00, nx10, u.y), nxy1 = glm::mix(nx01, nx11, u.y);
		return glm::mix(nxy0, nxy1, u.z); // ~[-1, 1]
	}
}