#pragma once
// Tiny CPU software rasterizer for offline LEGO part thumbnails.
//
// Header-only, glm+std only (plus stb_image_write for PNG output). Takes a baked
// mesh (positions/normals/indices in LDU) and renders a fixed 3/4 orthographic
// view to an RGBA PNG with a transparent background. No Vulkan / engine link.
//
// The single translation unit that includes this header must define
// STB_IMAGE_WRITE_IMPLEMENTATION before the include so the PNG encoder is emitted.

#include "ldraw_library.hpp"      // BakedMesh
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

namespace bagel::ldraw {

	namespace thumb_detail {

		inline float edge(float ax, float ay, float bx, float by, float cx, float cy) {
			return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
		}

		// A vertex projected into the thumbnail: screen pixel (x,y), depth, and the
		// model-space normal (lighting is done in model space so the light can track
		// the fixed camera).
		struct SV {
			float sx, sy, depth;
			glm::vec3 n;
		};

		// Quantized position key for welding the unwelded baked mesh (~1e-3 LDU grid,
		// well below LDraw's >=~1 LDU feature spacing).
		struct PosKey {
			int64_t x, y, z;
			bool operator==(const PosKey& o) const { return x == o.x && y == o.y && z == o.z; }
		};
		struct PosKeyHash {
			size_t operator()(const PosKey& k) const {
				size_t h = 1469598103934665603ull;
				for (int64_t v : { k.x, k.y, k.z }) { h ^= size_t(v); h *= 1099511628211ull; }
				return h;
			}
		};
		inline PosKey quantize(const glm::vec3& p) {
			return { int64_t(std::llround(p.x * 1000.0f)),
			         int64_t(std::llround(p.y * 1000.0f)),
			         int64_t(std::llround(p.z * 1000.0f)) };
		}

	} // namespace thumb_detail

	// Per-vertex smoothed normals with a crease-angle cutoff. Vertices sharing a
	// position blend only across faces whose dihedral angle is <= creaseDeg (studs /
	// cylinder facets round out; sharp brick edges stay crisp), each face weighted by
	// its corner angle so uneven tessellation doesn't bias the result. The baked mesh
	// is unwelded and flat-shaded; the returned vector is parallel to mesh.positions.
	inline std::vector<glm::vec3> smoothNormals(const BakedMesh& mesh, float creaseDeg) {
		using namespace thumb_detail;
		const size_t V = mesh.positions.size();
		const size_t T = mesh.indices.size() / 3;
		const float cosThresh = std::cos(creaseDeg * 3.14159265358979323846f / 180.0f);

		// Face normals + per-corner interior angles (weights).
		std::vector<glm::vec3> faceN(T);
		std::vector<float> cornerW(T * 3);
		for (size_t t = 0; t < T; ++t) {
			const uint32_t i0 = mesh.indices[t * 3 + 0];
			const uint32_t i1 = mesh.indices[t * 3 + 1];
			const uint32_t i2 = mesh.indices[t * 3 + 2];
			const glm::vec3& p0 = mesh.positions[i0];
			const glm::vec3& p1 = mesh.positions[i1];
			const glm::vec3& p2 = mesh.positions[i2];
			glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
			const float l = glm::length(n);
			faceN[t] = l > 1e-12f ? n / l : glm::vec3{ 0, 0, 1 };
			// Interior angle at each corner (between its two incident edges).
			const glm::vec3 P[3] = { p0, p1, p2 };
			for (int k = 0; k < 3; ++k) {
				glm::vec3 e1 = P[(k + 1) % 3] - P[k];
				glm::vec3 e2 = P[(k + 2) % 3] - P[k];
				const float l1 = glm::length(e1), l2 = glm::length(e2);
				cornerW[t * 3 + k] = (l1 > 1e-12f && l2 > 1e-12f)
					? std::acos(glm::clamp(glm::dot(e1, e2) / (l1 * l2), -1.0f, 1.0f))
					: 0.0f;
			}
		}

		// Bucket triangle-corners by welded position.
		std::unordered_map<PosKey, std::vector<uint32_t>, PosKeyHash> buckets;
		buckets.reserve(V);
		for (uint32_t c = 0; c < uint32_t(T * 3); ++c)
			buckets[quantize(mesh.positions[mesh.indices[c]])].push_back(c);

		// For each corner, blend the incident faces within the crease threshold.
		std::vector<glm::vec3> out(V, glm::vec3{ 0, 0, 1 });
		for (const auto& kv : buckets) {
			const std::vector<uint32_t>& group = kv.second;
			for (uint32_t c : group) {
				const glm::vec3 fn = faceN[c / 3];
				glm::vec3 acc{ 0.0f };
				for (uint32_t d : group) {
					const glm::vec3 dn = faceN[d / 3];
					if (glm::dot(fn, dn) >= cosThresh) acc += cornerW[d] * dn;
				}
				const float l = glm::length(acc);
				out[mesh.indices[c]] = l > 1e-12f ? acc / l : fn;
			}
		}
		return out;
	}

	// Render `mesh` to `outPath` as a size x size RGBA PNG. Internally rasterizes at
	// `ss`x supersampling and box-downsamples for antialiasing. Returns false if the
	// mesh is empty or the PNG can't be written.
	// `edgeDeg`: normal-discontinuity angle above which neighbouring pixels get an
	// inked black edge line (LDraw look). Set <= 0 to disable edge lines entirely.
	inline bool renderThumbnail(const BakedMesh& mesh, int size, const std::string& outPath,
	                            int ss = 2, float creaseDeg = 30.0f, float edgeDeg = 25.0f) {
		using namespace thumb_detail;
		if (mesh.positions.empty() || mesh.indices.size() < 3) return false;
		if (size < 1) size = 1;
		if (ss < 1) ss = 1;
		const int S = size * ss;   // supersampled resolution

		// Smoothed shading normals (crease-angle split); parallel to mesh.positions.
		const std::vector<glm::vec3> shadeN = smoothNormals(mesh, creaseDeg);

		// --- Camera basis: fixed 3/4 view. LDraw is -Y up (studs point toward -Y),
		// so world-up is (0,-1,0). Eye sits front-upper-right of the part center.
		const glm::vec3 worldUp{ 0.0f, -1.0f, 0.0f };
		const glm::vec3 viewFrom = glm::normalize(glm::vec3{ 1.0f, -1.0f, 1.0f }); // eye offset dir
		const glm::vec3 forward = -viewFrom;                                        // eye -> scene
		const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
		const glm::vec3 upv = glm::cross(right, forward);
		// Light: a key light from the upper-right toward the part, plus ambient. Kept
		// in model space; two-sided (abs) so thin/inward faces still read.
		const glm::vec3 lightDir = glm::normalize(viewFrom + worldUp * 0.15f);
		const float ambient = 0.28f;
		const glm::vec3 base{ 0.78f, 0.78f, 0.80f };

		// --- Bounding box -> center.
		glm::vec3 lo = mesh.positions[0], hi = mesh.positions[0];
		for (const glm::vec3& p : mesh.positions) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
		const glm::vec3 center = 0.5f * (lo + hi);

		// --- Project all vertices to camera plane; find the plane-space extent to fit.
		std::vector<SV> sv(mesh.positions.size());
		float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
		for (size_t i = 0; i < mesh.positions.size(); ++i) {
			// Rotate the part 180 deg about its vertical (stud) axis. LDraw up is -Y,
			// so the vertical axis is Y; a 180 deg turn about it negates X and Z.
			glm::vec3 rel = mesh.positions[i] - center;
			rel = glm::vec3{ -rel.x, rel.y, -rel.z };
			glm::vec3 nrm = shadeN[i];
			nrm = glm::vec3{ -nrm.x, nrm.y, -nrm.z };

			const float x = glm::dot(rel, right);
			const float y = glm::dot(rel, upv);
			sv[i].depth = glm::dot(rel, forward);   // larger = farther
			sv[i].n = nrm;
			sv[i].sx = x; sv[i].sy = y;
			minx = std::min(minx, x); maxx = std::max(maxx, x);
			miny = std::min(miny, y); maxy = std::max(maxy, y);
		}
		// Square, margined ortho frame from the larger plane span.
		const float cx = 0.5f * (minx + maxx), cy = 0.5f * (miny + maxy);
		float span = std::max(maxx - minx, maxy - miny) * 1.12f;   // ~12% margin
		if (span < 1e-6f) span = 1.0f;
		const float scale = (S - 1) / span;
		for (SV& v : sv) {
			v.sx = (v.sx - cx) * scale + S * 0.5f;
			v.sy = S * 0.5f - (v.sy - cy) * scale;   // flip Y for image space
		}

		// --- Rasterize (z-buffer + Lambert) into a supersampled RGBA buffer.
		std::vector<float> zbuf(size_t(S) * S, 1e30f);
		std::vector<glm::vec3> nbuf(size_t(S) * S, glm::vec3{ 0.0f });   // per-pixel normal (edge pass)
		std::vector<uint8_t> hires(size_t(S) * S * 4, 0);   // transparent background

		for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
			const SV& a = sv[mesh.indices[t + 0]];
			const SV& b = sv[mesh.indices[t + 1]];
			const SV& c = sv[mesh.indices[t + 2]];
			const float area = edge(a.sx, a.sy, b.sx, b.sy, c.sx, c.sy);
			if (std::fabs(area) < 1e-6f) continue;

			int x0 = std::max(0, (int)std::floor(std::min({ a.sx, b.sx, c.sx })));
			int x1 = std::min(S - 1, (int)std::ceil(std::max({ a.sx, b.sx, c.sx })));
			int y0 = std::max(0, (int)std::floor(std::min({ a.sy, b.sy, c.sy })));
			int y1 = std::min(S - 1, (int)std::ceil(std::max({ a.sy, b.sy, c.sy })));

			for (int py = y0; py <= y1; ++py) {
				for (int px = x0; px <= x1; ++px) {
					const float fx = px + 0.5f, fy = py + 0.5f;
					float w0 = edge(b.sx, b.sy, c.sx, c.sy, fx, fy);
					float w1 = edge(c.sx, c.sy, a.sx, a.sy, fx, fy);
					float w2 = edge(a.sx, a.sy, b.sx, b.sy, fx, fy);
					// Inside if all edge signs agree (handles either winding).
					const bool in = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
					if (!in) continue;
					const float b0 = w0 / area, b1 = w1 / area, b2 = w2 / area;
					const float depth = b0 * a.depth + b1 * b.depth + b2 * c.depth;
					const size_t idx = size_t(py) * S + px;
					if (depth >= zbuf[idx]) continue;
					zbuf[idx] = depth;

					glm::vec3 n = b0 * a.n + b1 * b.n + b2 * c.n;
					const float nl = glm::length(n);
					n = nl > 1e-6f ? n / nl : glm::vec3{ 0, 0, 1 };
					nbuf[idx] = n;
					const float diff = std::fabs(glm::dot(n, lightDir));   // two-sided
					const float lit = ambient + (1.0f - ambient) * diff;
					const glm::vec3 col = glm::clamp(base * lit, 0.0f, 1.0f);
					uint8_t* px4 = &hires[idx * 4];
					px4[0] = uint8_t(col.r * 255.0f + 0.5f);
					px4[1] = uint8_t(col.g * 255.0f + 0.5f);
					px4[2] = uint8_t(col.b * 255.0f + 0.5f);
					px4[3] = 255;
				}
			}
		}

		// --- Edge pass: ink sharp edges black before downsampling (AA comes for free).
		// A supersampled pixel becomes a black line if it borders the background
		// (silhouette / outline), if depth jumps to a neighbour (one surface in front of
		// another), or if the shading normal flips past edgeDeg (a geometric crease).
		if (edgeDeg > 0.0f) {
			const float cosEdge = std::cos(edgeDeg * 3.14159265358979323846f / 180.0f);
			const float depthJump = span * 0.03f;   // LDU; overlap vs. smooth-slope cutoff
			std::vector<uint8_t> edgeMask(size_t(S) * S, 0);
			const int off[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
			for (int py = 0; py < S; ++py) {
				for (int px = 0; px < S; ++px) {
					const size_t idx = size_t(py) * S + px;
					if (hires[idx * 4 + 3] == 0) continue;   // background: never inked
					bool isEdge = false;
					for (int k = 0; k < 4 && !isEdge; ++k) {
						const int nx = px + off[k][0], ny = py + off[k][1];
						if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
						const size_t nid = size_t(ny) * S + nx;
						if (hires[nid * 4 + 3] == 0) { isEdge = true; break; }   // silhouette
						if (zbuf[nid] - zbuf[idx] > depthJump) { isEdge = true; break; } // occlusion
						if (glm::dot(nbuf[idx], nbuf[nid]) < cosEdge) isEdge = true;      // crease
					}
					edgeMask[idx] = isEdge ? 1 : 0;
				}
			}
			for (size_t idx = 0; idx < edgeMask.size(); ++idx) {
				if (!edgeMask[idx]) continue;
				uint8_t* p = &hires[idx * 4];
				p[0] = p[1] = p[2] = 0; p[3] = 255;
			}
		}

		// --- Box-downsample ss x ss -> final RGBA (edges get partial alpha => AA).
		std::vector<uint8_t> out(size_t(size) * size * 4);
		const int n2 = ss * ss;
		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				int r = 0, g = 0, bl = 0, al = 0;
				for (int sy = 0; sy < ss; ++sy)
					for (int sx = 0; sx < ss; ++sx) {
						const uint8_t* p = &hires[(size_t(y * ss + sy) * S + (x * ss + sx)) * 4];
						r += p[0]; g += p[1]; bl += p[2]; al += p[3];
					}
				uint8_t* o = &out[(size_t(y) * size + x) * 4];
				o[0] = uint8_t(r / n2); o[1] = uint8_t(g / n2);
				o[2] = uint8_t(bl / n2); o[3] = uint8_t(al / n2);
			}
		}

		return stbi_write_png(outPath.c_str(), size, size, 4, out.data(), size * 4) != 0;
	}

} // namespace bagel::ldraw
