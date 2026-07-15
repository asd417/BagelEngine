#pragma once
// ImGui browser for the LEGO part catalog: a scrollable, filterable grid of part thumbnails.
// Consumes PartCatalog (the catalog) and BGLTextureStreamer (async thumbnails). Only the
// on-screen cells request/keep textures (ImGuiListClipper), so browsing thousands of parts
// stays cheap. This is the thin ImGui *adapter* over the engine-level streamer: it maps
// part name -> ImTextureID (ImGui_ImplVulkan_AddTexture) and releases them on the streamer's
// evict hook. Placement is left to the caller via setOnPick.

#include "part_catalog.hpp"
#include "texture/bagel_texture_streamer.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bagel {

	class LegoBrowserPanel {
	public:
		LegoBrowserPanel(ldraw::PartCatalog& library, BGLTextureStreamer& streamer);
		~LegoBrowserPanel();
		LegoBrowserPanel(const LegoBrowserPanel&) = delete;
		LegoBrowserPanel& operator=(const LegoBrowserPanel&) = delete;

		// Build the ImGui window (call inside the ImGui frame). `open` toggles visibility.
		void draw(bool* open = nullptr);

		// Fired when a thumbnail is clicked. Wire this to spawn/place the part.
		void setOnPick(std::function<void(const ldraw::PartCatalogEntry&)> cb) { onPick_ = std::move(cb); }
		const std::string& selected() const { return selected_; }

	private:
		// ImU64 (== ImTextureID); avoids pulling imgui.h into this header.
		using TexId = unsigned long long;
		TexId imTextureFor(const std::string& name, const BGLTextureStreamer::ResidentTexture& t);

		ldraw::PartCatalog&  library_;
		BGLTextureStreamer&  streamer_;
		std::unordered_map<std::string, TexId> imTex_;   // name -> ImGui descriptor (derived handle)
		std::vector<int>     filtered_;                  // indices into library_.entries(), rebuilt per draw
		std::function<void(const ldraw::PartCatalogEntry&)> onPick_;
		std::string          selected_;
		char                 filter_[64] = {0};
		int                  sortMode_ = 0;              // 0 = name, 1 = title
		float                thumbPx_ = 64.0f;
	};

} // namespace bagel
