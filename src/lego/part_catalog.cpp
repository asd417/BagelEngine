#include "part_catalog.hpp"
#include "ldraw_library.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace bagel::ldraw {

	namespace {
		

		// LDraw redirect stubs ("0 ~Moved to 3665") are renamed-part aliases, not real pieces --
		// they bake connectors from their target but must not appear in the catalog.
		bool isRedirect(const std::string& title) {
			std::string head = title.substr(0, 8);
			std::transform(head.begin(), head.end(), head.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return head.rfind("moved to", 0) == 0;
		}
	}

	size_t PartCatalog::scan(const std::string& engineDir) {
		entries_.clear();
		skipped_ = 0;

		const fs::path base   = fs::path(engineDir) / "lego";
		const fs::path connDir = base / "baked" / "connections";
		const fs::path thumbDir = base / "baked" / "thumbnails";
		const fs::path collDir = base / "baked" / "collision";
		const fs::path partsDir = base / "ldraw" / "parts";

		if (!fs::exists(connDir)) return 0;

		for (const auto& e : fs::directory_iterator(connDir)) {
			if (!e.is_regular_file() || e.path().extension() != ".conn") continue;
			const std::string name = e.path().stem().string();

			const fs::path png = thumbDir / (name + ".png");
			const fs::path glb = collDir / (name + ".glb");
			const fs::path dat = partsDir / (name + ".dat");
			if (!fs::exists(png) || !fs::exists(glb) || !fs::exists(dat)) { ++skipped_; continue; }

			std::string title = readTitle(dat);
			if (isRedirect(title)) { ++skipped_; continue; }   // renamed-part alias stub

			PartCatalogEntry entry;
			entry.name          = name;
			entry.title         = std::move(title);
			entry.datPath       = dat.string();
			entry.thumbPath     = png.string();
			entry.connPath      = e.path().string();
			entry.collisionPath = glb.string();
			entries_.push_back(std::move(entry));
		}

		sortByName();
		return entries_.size();
	}

	void PartCatalog::sortByName() {
		std::sort(entries_.begin(), entries_.end(),
			[](const PartCatalogEntry& a, const PartCatalogEntry& b) { return a.name < b.name; });
	}

	void PartCatalog::sortByTitle() {
		std::sort(entries_.begin(), entries_.end(),
			[](const PartCatalogEntry& a, const PartCatalogEntry& b) {
				if (a.title != b.title) return a.title < b.title;
				return a.name < b.name;
			});
	}

} // namespace bagel::ldraw
