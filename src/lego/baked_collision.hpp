#pragma once
// Engine-side reader for the offline collision bake (lego/baked/collision/<part>.glb,
// produced by partsParser). Each GLB holds a part's convex collision hull(s) as glTF
// mesh primitives, positions in raw LDU (studs-down, matching the render mesh before
// the engine's load-time scale). Phase 1 bakes one hull per part; the reader already
// returns a list so Phase 2's CoACD multi-hull decomposition needs no change here.
//
// Files are loaded + cached on demand (per-part, unlike the single connectors.bin),
// so only the bricks actually spawned pay the parse cost.

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bagel::ldraw {

	class BakedCollision {
	public:
		// Directory holding <part>.glb files (e.g. ".../lego/baked/collision").
		void setDir(std::string dir) { dir_ = std::move(dir); }

		// Convex hull point sets for a part ("3001", "3001.dat" -> normalized to the bare
		// lowercased stem the baker named the file). One entry per hull; each is the hull's
		// vertices in raw LDU. Loads + caches <dir>/<stem>.glb on first request. nullptr if
		// the file is missing or unparseable.
		const std::vector<std::vector<glm::vec3>>* find(const std::string& partName);

	private:
		std::string dir_;
		std::unordered_map<std::string, std::vector<std::vector<glm::vec3>>> cache_;
		std::unordered_set<std::string> missing_;   // negative cache (avoid re-stat'ing)
	};

} // namespace bagel::ldraw
