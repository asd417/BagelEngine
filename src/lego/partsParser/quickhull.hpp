#pragma once
// Minimal 3D convex hull (QuickHull) for the offline LEGO collision baker.
//
// Header-only, glm + std only. Takes a point cloud and returns the convex hull as
// an indexed triangle mesh (vertices + 3-per-triangle indices, outward-facing CCW).
// Tuned for correctness + simplicity on clean, boxy LEGO geometry rather than for
// giant point clouds; it welds duplicate input points first, so the working set is
// the handful of distinct part corners.
//
// Orientation is anchored to a fixed interior point (the seed-tetra centroid, which
// stays strictly inside the hull), so face winding never has to be tracked across
// iterations and the horizon is found by a simple undirected-edge count over the
// apex-visible faces -- robust against the winding drift that a directed half-edge
// scheme is prone to.
//
// Not a general-purpose robust hull: perfectly coplanar inputs (a flat part with no
// thickness) can't seed a tetrahedron -- convexHull() reports that via `ok=false`
// and the caller falls back to emitting the raw welded points (Jolt re-hulls them
// at load anyway).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

namespace bagel::ldraw::qh {

	struct HullMesh {
		std::vector<glm::vec3>  verts;    // hull vertices (subset of the welded input)
		std::vector<uint32_t>   indices;  // 3 per triangle, CCW when viewed from outside
		bool ok = false;                  // false => degenerate (coplanar/too few points)
	};

	namespace detail {

		// A hull face: three indices into the shared point array, an outward plane, and
		// the set of not-yet-processed points that lie in front of it (its "outside set").
		struct Face {
			uint32_t a, b, c;
			glm::vec3 n;      // unit outward normal
			float d;          // plane offset: dot(n, x) - d == signed distance to the plane
			std::vector<uint32_t> outside;
			bool dead = false;
		};

		inline void makePlane(Face& f, const std::vector<glm::vec3>& p) {
			f.n = glm::cross(p[f.b] - p[f.a], p[f.c] - p[f.a]);
			const float l = glm::length(f.n);
			f.n = l > 1e-20f ? f.n / l : glm::vec3{ 0, 0, 1 };
			f.d = glm::dot(f.n, p[f.a]);
		}
		inline float dist(const Face& f, const glm::vec3& x) { return glm::dot(f.n, x) - f.d; }

		// Build a face and flip it so its normal points away from `interior` (outward).
		inline Face makeFace(uint32_t a, uint32_t b, uint32_t c,
		                     const std::vector<glm::vec3>& p, const glm::vec3& interior) {
			Face f; f.a = a; f.b = b; f.c = c; makePlane(f, p);
			if (dist(f, interior) > 0.0f) { std::swap(f.b, f.c); makePlane(f, p); }
			return f;
		}

		// Weld near-duplicate points onto a ~1e-3 LDU grid so the hull works on the few
		// distinct corners rather than the unwelded per-triangle vertex soup.
		inline std::vector<glm::vec3> weld(const std::vector<glm::vec3>& in) {
			std::unordered_map<uint64_t, uint32_t> seen;
			seen.reserve(in.size());
			std::vector<glm::vec3> out;
			auto key = [](const glm::vec3& v) {
				auto q = [](float f) { return uint64_t(int64_t(std::llround(f * 1000.0f)) & 0x1FFFFF); };
				return (q(v.x) << 42) ^ (q(v.y) << 21) ^ q(v.z);
			};
			for (const glm::vec3& v : in)
				if (seen.emplace(key(v), uint32_t(out.size())).second) out.push_back(v);
			return out;
		}

	} // namespace detail

	// Build the convex hull of `points`. `epsScale` multiplies the automatic tolerance
	// (derived from the point-cloud extent) if you need to loosen/tighten coplanarity.
	inline HullMesh convexHull(const std::vector<glm::vec3>& points, float epsScale = 1.0f) {
		using namespace detail;
		HullMesh result;
		std::vector<glm::vec3> p = weld(points);
		if (p.size() < 4) return result;   // need a tetrahedron

		// Tolerance from the bounding-box diagonal.
		glm::vec3 lo = p[0], hi = p[0];
		for (const glm::vec3& v : p) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
		const float diag = glm::length(hi - lo);
		const float eps = std::max(1e-4f, 1e-5f * diag) * epsScale;

		// --- Seed tetrahedron: two extreme points, the farthest from that line, then the
		// farthest from that triangle's plane.
		uint32_t i0 = 0, i1 = 0;
		{
			std::array<uint32_t, 6> ex{ 0,0,0,0,0,0 };
			for (uint32_t i = 1; i < p.size(); ++i) {
				if (p[i].x < p[ex[0]].x) ex[0] = i;  if (p[i].x > p[ex[1]].x) ex[1] = i;
				if (p[i].y < p[ex[2]].y) ex[2] = i;  if (p[i].y > p[ex[3]].y) ex[3] = i;
				if (p[i].z < p[ex[4]].z) ex[4] = i;  if (p[i].z > p[ex[5]].z) ex[5] = i;
			}
			float best = -1.0f;
			for (uint32_t a : ex) for (uint32_t b : ex) {
				const float dd = glm::length(p[a] - p[b]);
				if (dd > best) { best = dd; i0 = a; i1 = b; }
			}
			if (best <= eps) return result;   // all points coincident
		}
		uint32_t i2 = 0; { float best = -1.0f;
			const glm::vec3 e = glm::normalize(p[i1] - p[i0]);
			for (uint32_t i = 0; i < p.size(); ++i) {
				const glm::vec3 d = p[i] - p[i0];
				const float area = glm::length(d - e * glm::dot(d, e));   // dist to the line
				if (area > best) { best = area; i2 = i; }
			}
			if (best <= eps) return result;   // collinear
		}
		uint32_t i3 = 0; {
			const glm::vec3 triN = glm::normalize(glm::cross(p[i1] - p[i0], p[i2] - p[i0]));
			float best = -1.0f;
			for (uint32_t i = 0; i < p.size(); ++i) {
				const float dd = std::fabs(glm::dot(p[i] - p[i0], triN));
				if (dd > best) { best = dd; i3 = i; }
			}
			if (best <= eps) return result;   // coplanar -> degenerate, caller falls back
		}

		// Interior anchor: the seed centroid stays strictly inside the growing hull, so
		// every face can be oriented outward against it.
		const glm::vec3 interior = (p[i0] + p[i1] + p[i2] + p[i3]) * 0.25f;
		std::vector<Face> faces;
		faces.push_back(makeFace(i0, i1, i2, p, interior));
		faces.push_back(makeFace(i0, i1, i3, p, interior));
		faces.push_back(makeFace(i0, i2, i3, p, interior));
		faces.push_back(makeFace(i1, i2, i3, p, interior));

		// Initial outside sets: assign each point to the first face it is clearly in front of.
		for (uint32_t i = 0; i < p.size(); ++i) {
			if (i == i0 || i == i1 || i == i2 || i == i3) continue;
			for (Face& f : faces) if (dist(f, p[i]) > eps) { f.outside.push_back(i); break; }
		}

		auto ukey = [](uint32_t a, uint32_t b) { if (a > b) std::swap(a, b); return (uint64_t(a) << 32) | b; };

		// --- Expand: each pass adds exactly one hull vertex (the farthest outside point),
		// so at most p.size() passes.
		for (;;) {
			int fi = -1;
			for (int i = 0; i < (int)faces.size(); ++i)
				if (!faces[i].dead && !faces[i].outside.empty()) { fi = i; break; }
			if (fi < 0) break;   // no face has points in front -> hull complete

			// Farthest point above this face = the apex to add.
			uint32_t apex = 0; float far = -1.0f;
			for (uint32_t idx : faces[fi].outside) {
				const float dd = dist(faces[fi], p[idx]);
				if (dd > far) { far = dd; apex = idx; }
			}

			// Faces the apex can "see" get removed; count their edges and orphan their points.
			std::unordered_map<uint64_t, int> edgeCount;
			std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> edgeDir;
			std::vector<uint32_t> orphans;
			for (Face& f : faces) {
				if (f.dead || dist(f, p[apex]) <= eps) continue;
				const uint32_t v[3] = { f.a, f.b, f.c };
				for (int k = 0; k < 3; ++k) {
					const uint32_t u = v[k], w = v[(k + 1) % 3];
					const uint64_t key = ukey(u, w);
					edgeCount[key]++; edgeDir[key] = { u, w };
				}
				for (uint32_t idx : f.outside) if (idx != apex) orphans.push_back(idx);
				f.dead = true; f.outside.clear();
			}

			// Horizon = edges bordering exactly one visible face; fan the apex to each.
			const size_t firstNew = faces.size();
			for (const auto& kv : edgeCount) {
				if (kv.second != 1) continue;
				const auto& e = edgeDir[kv.first];
				faces.push_back(makeFace(e.first, e.second, apex, p, interior));
			}
			// Re-home orphaned points onto the new faces.
			for (uint32_t idx : orphans) {
				for (size_t i = firstNew; i < faces.size(); ++i)
					if (dist(faces[i], p[idx]) > eps) { faces[i].outside.push_back(idx); break; }
			}
		}

		// --- Collect live faces, compacting to the used vertices.
		std::unordered_map<uint32_t, uint32_t> remap;
		auto emit = [&](uint32_t v) {
			auto it = remap.find(v);
			if (it != remap.end()) return it->second;
			uint32_t nv = uint32_t(result.verts.size());
			remap.emplace(v, nv); result.verts.push_back(p[v]); return nv;
		};
		for (const Face& f : faces) {
			if (f.dead) continue;
			result.indices.push_back(emit(f.a));
			result.indices.push_back(emit(f.b));
			result.indices.push_back(emit(f.c));
		}
		result.ok = result.verts.size() >= 4 && !result.indices.empty();
		return result;
	}

} // namespace bagel::ldraw::qh
