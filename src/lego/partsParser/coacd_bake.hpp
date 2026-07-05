#pragma once
// CoACD approximate convex decomposition, invoked out-of-process (Phase 2).
//
// Rather than link CoACD's heavy C++ (an ABI mismatch with this MinGW/g++ tool and a
// per-toolchain CMake build), the baker shells out to a small Python helper backed by
// the pip `coacd` package -- prebuilt wheels exist for Windows/Linux/Mac, so this is
// the portable path. See setup_coacd.bat / .sh to create the venv.
//
// Flow: write the part mesh to a temp OBJ -> run coacd_decompose.py -> read the convex
// pieces (one OBJ, one `o` group per piece) -> re-hull each with our own QuickHull so
// the stored collider is a tight minimal hull rather than CoACD's tessellation.

#include "ldraw_library.hpp"     // BakedMesh
#include "mesh_collision.hpp"    // CollisionHull, hullFromPoints

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

namespace bagel::ldraw {

	struct CoacdConfig {
		std::string python;         // venv python interpreter
		std::string helper;         // coacd_decompose.py
		double threshold = 20.0;    // CoACD concavity threshold (in LDU when realMetric is on)
		int    maxHulls  = -1;      // -1 = unlimited (CoACD's max_convex_hull)
		int    resolution = 30;     // preprocess remesh resolution (lower = faster on simple parts)
		int    mctsIter  = 100;     // MCTS iterations (lower = faster)
		bool   decimate  = true;    // decimate each output hull to <= maxChVertex verts
		int    maxChVertex = 64;    // per-hull vertex cap when decimate is on
		bool   realMetric = true;   // interpret `threshold` in the mesh's real units (LDU)
	};

	namespace coacd_detail {

		inline void writeObj(const std::string& path, const BakedMesh& mesh) {
			std::ofstream os(path);
			os << "# BagelEngine part mesh for CoACD (LDU)\n";
			for (const glm::vec3& v : mesh.positions)
				os << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
			for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
				os << "f " << (mesh.indices[t] + 1) << ' '
				   << (mesh.indices[t + 1] + 1) << ' ' << (mesh.indices[t + 2] + 1) << '\n';
		}

		// Read a piece OBJ: each `o` group's `v` lines become one point cloud. The helper
		// emits vertices grouped directly under each `o`, so collecting by current group
		// (ignoring faces) is sufficient.
		inline std::vector<std::vector<glm::vec3>> readGroups(const std::string& path) {
			std::vector<std::vector<glm::vec3>> groups;
			std::ifstream is(path);
			std::string line;
			while (std::getline(is, line)) {
				if (line.size() >= 2 && line[0] == 'o' && line[1] == ' ') groups.emplace_back();
				else if (line.size() >= 2 && line[0] == 'v' && line[1] == ' ') {
					if (groups.empty()) groups.emplace_back();
					float x, y, z;
					if (std::sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z) == 3)
						groups.back().push_back({ x, y, z });
				}
			}
			return groups;
		}

		// Run `python helper in out threshold maxHulls`. Quoting handles the spaces in the
		// engine path (e.g. "OneDrive\Documents"); on Windows cmd needs the whole command
		// wrapped in an extra pair of quotes.
		inline int run(const CoacdConfig& cfg, const std::string& in, const std::string& out) {
			auto q = [](const std::string& s) { return "\"" + s + "\""; };
			std::string cmd = q(cfg.python) + " " + q(cfg.helper) + " " + q(in) + " " + q(out)
			                + " " + std::to_string(cfg.threshold) + " " + std::to_string(cfg.maxHulls)
			                + " " + std::to_string(cfg.resolution) + " " + std::to_string(cfg.mctsIter)
			                + " " + (cfg.decimate ? "1" : "0") + " " + std::to_string(cfg.maxChVertex)
			                + " " + (cfg.realMetric ? "1" : "0");
#ifdef _WIN32
			cmd = "\"" + cmd + "\"";
#endif
			return std::system(cmd.c_str());
		}

	} // namespace coacd_detail

	// Decompose `mesh` into convex hulls via CoACD. Returns the (re-hulled) pieces, or an
	// empty vector on any failure (caller falls back to a single hull). `ok` reports success.
	inline std::vector<CollisionHull> coacdHulls(const BakedMesh& mesh, const std::string& partName,
	                                             const CoacdConfig& cfg, bool& ok) {
		namespace fs = std::filesystem;
		using namespace coacd_detail;
		ok = false;
		std::vector<CollisionHull> hulls;

		static int counter = 0;
		fs::path tmp = fs::temp_directory_path();
		const std::string tag = "bgl_coacd_" + partName + "_" + std::to_string(counter++);
		const std::string inObj  = (tmp / (tag + "_in.obj")).string();
		const std::string outObj = (tmp / (tag + "_out.obj")).string();

		writeObj(inObj, mesh);
		const int rc = run(cfg, inObj, outObj);
		if (rc == 0 && fs::exists(outObj)) {
			for (auto& g : readGroups(outObj)) {
				if (g.size() < 4) continue;
				CollisionHull h = hullFromPoints(g);
				if (!h.verts.empty()) hulls.push_back(std::move(h));
			}
			ok = !hulls.empty();
		}
		std::error_code ec;
		fs::remove(inObj, ec); fs::remove(outObj, ec);
		return hulls;
	}

} // namespace bagel::ldraw
