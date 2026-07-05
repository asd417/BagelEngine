#pragma once
// Engine-side reader for the offline connector bake (lego/baked/connectors/<part>.conn,
// produced by partsParser). One human-readable text file per part -- so a human can hand
// author or fix connectors for parts the baker can't detect (male axles/pins, the large
// Constraction/Bionicle balls, raw-modeled rotation joints). See src/lego/CONNECTORS.md.
//
// File format (plain text, one connector per line, '#' comments, blank lines ignored):
//   type family detents  px py pz  ax ay az
//     type    = male|female|pin|axle|ball|socket
//     family  = none|joint8|constraction|clickhinge|duplo
//     detents = click-hinge stop count ("N-Position"), else 0
//     pos     = part-local LDU (studs-down, pre-scale)
//     ax..az  = unit connector axis (+Y / stud direction); the reader rebuilds a full basis.
//   Line 1 is a "# source: baked|manual" header; the baker skips files marked manual.
//
// axisGroup is NOT stored -- it is derived from geometry, so the reader computes it on load
// (assignAxisGroups), which also makes hand-authored parts get correct collinearity for free.
//
// Files are loaded + cached on demand (per-part, like BakedCollision), so only the bricks
// actually spawned pay the parse cost.

#include "ldraw_library.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bagel::ldraw {

	class BakedConnectors {
	public:
		// Directory holding <part>.conn files (e.g. ".../lego/baked/connectors").
		void setDir(std::string dir) { dir_ = std::move(dir); }

		// Connectors for a part ("3001", "3001.dat", "s/..." -> normalized to the bare
		// lowercased stem). Loads + caches <dir>/<stem>.conn on first request, computing
		// axisGroup and rebuilding orient. nullptr if the file is missing or unparseable
		// (which, in a complete bake, means the part has no connectors).
		const std::vector<ConnectionPoint>* find(const std::string& partName);

	private:
		std::string dir_;
		std::unordered_map<std::string, std::vector<ConnectionPoint>> cache_;
		std::unordered_set<std::string> missing_;   // negative cache (avoid re-stat'ing)
	};

} // namespace bagel::ldraw
