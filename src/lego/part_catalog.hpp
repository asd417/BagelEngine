#pragma once
// Catalog of placeable LEGO parts. Scans the offline-baked asset dirs and keeps ONLY parts
// that have all of: a thumbnail PNG, a connectors .conn, a collision .glb, and the source
// .dat. Anything missing one of these is skipped -- so every catalog entry is guaranteed to
// be fully placeable. This is the "catalog + lazy geometry" model: scanning only checks file
// existence and reads each part's title, it does NOT parse geometry. The (expensive) mesh /
// collision / connector parse happens later, when a part is actually placed (via the engine's
// ModelComponentBuilder + BakedCollision + BakedConnectors, all keyed by part name).
//
// Engine-agnostic (std + filesystem only), so it can be unit-tested standalone.

#include <string>
#include <vector>

namespace bagel::ldraw {

	struct PartCatalogEntry {
		std::string name;           // bare stem, e.g. "3001"
		std::string title;          // .dat title line, e.g. "Brick  2 x  4" (for display/sort)
		std::string datPath;        // parts/<name>.dat
		std::string thumbPath;      // baked/thumbnails/<name>.png
		std::string connPath;       // baked/connections/<name>.conn
		std::string collisionPath;  // baked/collision/<name>.glb
	};

	class PartCatalog {
	public:
		// Scan under `engineDir` (the folder containing lego/). Drives off the connections dir
		// (a placeable part must have connectors), requiring each part to also have a thumbnail,
		// a collision glb, and a source .dat. Populates entries(), sorted by name. Returns the
		// count kept. Re-scanning clears the previous result.
		size_t scan(const std::string& engineDir);

		const std::vector<PartCatalogEntry>& entries() const { return entries_; }
		size_t size() const { return entries_.size(); }
		size_t skipped() const { return skipped_; }   // parts with a .conn but a missing sidecar

		void sortByName();
		void sortByTitle();

	private:
		std::vector<PartCatalogEntry> entries_;
		size_t skipped_ = 0;
	};

} // namespace bagel::ldraw
