#pragma once
// Offline collision-model baking for LEGO parts (Phase 1: single convex hull).
//
// Header-only, glm + std only. computeHull() convex-hulls a baked part mesh; the
// hull naturally "fills in" stud / tube / pin holes, so for the (large) majority of
// bricks that are convex-once-filled this is a faithful collider. Genuinely concave
// parts get a loose enclosing hull for now -- Phase 2 will split those with CoACD and
// emit multiple hull primitives into the same GLB (the writer already takes N hulls).
//
// writeCollisionGlb() emits a self-contained binary glTF (.glb): one mesh, one
// primitive per hull, positions in raw LDU (studs-down, matching the render mesh
// before the engine's load-time scale). Editable in Blender / any glTF viewer; the
// engine re-hulls the points via Jolt on load, so a concave hand-edit is silently
// re-convexified.

#include "ldraw_library.hpp"   // BakedMesh
#include "quickhull.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

namespace bagel::ldraw {

	struct CollisionHull {
		std::vector<glm::vec3> verts;
		std::vector<uint32_t>  indices;   // 3 per triangle; empty => degenerate point cloud
	};

	// Convex hull of a raw point cloud. On a degenerate (coplanar) set the hull can't be
	// triangulated, so we fall back to the welded points with no indices -- still a valid
	// collider seed since Jolt hulls the points itself. Used both for a whole part and to
	// "optimize" (tighten/minimize) each convex piece CoACD returns.
	inline CollisionHull hullFromPoints(const std::vector<glm::vec3>& points) {
		CollisionHull out;
		qh::HullMesh h = qh::convexHull(points);
		if (h.ok) { out.verts = std::move(h.verts); out.indices = std::move(h.indices); }
		else       { out.verts = qh::detail::weld(points); }
		return out;
	}

	inline CollisionHull computeHull(const BakedMesh& mesh) { return hullFromPoints(mesh.positions); }

	// Signed volume of a closed triangle mesh (divergence theorem). Reliable given the
	// baker's consistent outward winding; caller takes fabs. Enclosed tube/hole tunnels
	// correctly subtract, so this measures the *solid* volume.
	inline double signedVolume(const std::vector<glm::vec3>& p, const std::vector<uint32_t>& idx) {
		double v = 0.0;
		for (size_t t = 0; t + 2 < idx.size(); t += 3) {
			const glm::vec3& a = p[idx[t]]; const glm::vec3& b = p[idx[t + 1]]; const glm::vec3& c = p[idx[t + 2]];
			v += double(glm::dot(a, glm::cross(b, c)));
		}
		return v / 6.0;
	}

	// Concavity in [0,1]: fraction of the convex hull's volume that the part does NOT fill
	// (0 = perfectly convex, higher = more concave). meshVol/hullVol out-params in LDU^3.
	inline double concavity(const BakedMesh& mesh, double* meshVol = nullptr, double* hullVol = nullptr) {
		const double mv = std::fabs(signedVolume(mesh.positions, mesh.indices));
		CollisionHull h = computeHull(mesh);
		const double hv = h.indices.empty() ? 0.0 : std::fabs(signedVolume(h.verts, h.indices));
		if (meshVol) *meshVol = mv;
		if (hullVol) *hullVol = hv;
		return hv > 1e-9 ? 1.0 - mv / hv : 0.0;
	}

	namespace glb_detail {
		template <class T> inline void push(std::vector<uint8_t>& b, const T& v) {
			const size_t o = b.size(); b.resize(o + sizeof(T)); std::memcpy(b.data() + o, &v, sizeof(T));
		}
		inline void pad4(std::vector<uint8_t>& b, uint8_t fill) { while (b.size() & 3) b.push_back(fill); }
	} // namespace glb_detail

	// Write `hulls` to `path` as a binary glTF. Returns false if the file can't be opened
	// or every hull is empty.
	inline bool writeCollisionGlb(const std::vector<CollisionHull>& hulls, const std::string& path) {
		using namespace glb_detail;

		// --- Binary buffer + bufferView/accessor/primitive JSON, built in lockstep.
		std::vector<uint8_t> bin;
		std::string bufferViews, accessors, primitives;
		int bvCount = 0, accCount = 0;
		bool any = false;

		auto f2s = [](float f) { char t[32]; std::snprintf(t, sizeof(t), "%.7g", f); return std::string(t); };

		for (const CollisionHull& hull : hulls) {
			if (hull.verts.empty()) continue;
			any = true;

			// Positions.
			glm::vec3 lo = hull.verts[0], hi = hull.verts[0];
			const size_t posOff = bin.size();
			for (const glm::vec3& v : hull.verts) {
				push(bin, v.x); push(bin, v.y); push(bin, v.z);
				lo = glm::min(lo, v); hi = glm::max(hi, v);
			}
			if (!bufferViews.empty()) bufferViews += ",";
			bufferViews += "{\"buffer\":0,\"byteOffset\":" + std::to_string(posOff) +
			               ",\"byteLength\":" + std::to_string(hull.verts.size() * 12) +
			               ",\"target\":34962}";
			const int posAcc = accCount++;
			const int posBv  = bvCount++;
			if (!accessors.empty()) accessors += ",";
			accessors += "{\"bufferView\":" + std::to_string(posBv) +
			             ",\"componentType\":5126,\"count\":" + std::to_string(hull.verts.size()) +
			             ",\"type\":\"VEC3\",\"min\":[" + f2s(lo.x) + "," + f2s(lo.y) + "," + f2s(lo.z) +
			             "],\"max\":[" + f2s(hi.x) + "," + f2s(hi.y) + "," + f2s(hi.z) + "]}";

			// Indices (optional -> POINTS primitive when absent).
			int idxAcc = -1;
			if (!hull.indices.empty()) {
				const size_t idxOff = bin.size();
				for (uint32_t i : hull.indices) push(bin, i);
				bufferViews += ",{\"buffer\":0,\"byteOffset\":" + std::to_string(idxOff) +
				               ",\"byteLength\":" + std::to_string(hull.indices.size() * 4) +
				               ",\"target\":34963}";
				const int idxBv = bvCount++;
				idxAcc = accCount++;
				accessors += ",{\"bufferView\":" + std::to_string(idxBv) +
				             ",\"componentType\":5125,\"count\":" + std::to_string(hull.indices.size()) +
				             ",\"type\":\"SCALAR\"}";
			}

			if (!primitives.empty()) primitives += ",";
			primitives += "{\"attributes\":{\"POSITION\":" + std::to_string(posAcc) + "}";
			if (idxAcc >= 0) primitives += ",\"indices\":" + std::to_string(idxAcc) + ",\"mode\":4";
			else             primitives += ",\"mode\":0";   // POINTS
			primitives += "}";
		}
		if (!any) return false;

		pad4(bin, 0x00);
		std::string json =
			"{\"asset\":{\"version\":\"2.0\",\"generator\":\"BagelEngine collision baker (convex hull, LDU)\"},"
			"\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],"
			"\"bufferViews\":[" + bufferViews + "],"
			"\"accessors\":[" + accessors + "],"
			"\"meshes\":[{\"primitives\":[" + primitives + "]}],"
			"\"nodes\":[{\"mesh\":0}],"
			"\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
		std::vector<uint8_t> jsonBytes(json.begin(), json.end());
		pad4(jsonBytes, 0x20);   // JSON chunk padded with spaces

		// --- Assemble the GLB container.
		const uint32_t total = 12 + 8 + uint32_t(jsonBytes.size()) + 8 + uint32_t(bin.size());
		std::ofstream os(path, std::ios::binary | std::ios::trunc);
		if (!os) return false;
		auto u32 = [&](uint32_t v) { os.write(reinterpret_cast<const char*>(&v), 4); };
		u32(0x46546C67); u32(2); u32(total);                                  // header: "glTF", ver 2, length
		u32(uint32_t(jsonBytes.size())); u32(0x4E4F534A);                     // JSON chunk header
		os.write(reinterpret_cast<const char*>(jsonBytes.data()), jsonBytes.size());
		u32(uint32_t(bin.size())); u32(0x004E4942);                          // BIN chunk header
		os.write(reinterpret_cast<const char*>(bin.data()), bin.size());
		return bool(os);
	}

} // namespace bagel::ldraw
