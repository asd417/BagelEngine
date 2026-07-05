#include "lego_browser_panel.hpp"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <cctype>

#define SELECTION_TINT ImVec4(0.4f, 0.4f, 1, 1)

namespace bagel {

	namespace {
		std::string toLower(std::string s) {
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
	}

	LegoBrowserPanel::LegoBrowserPanel(ldraw::PartCatalog& library, BGLTextureStreamer& streamer)
		: library_(library), streamer_(streamer)
	{
		// When the streamer forgets a thumbnail (LRU), release the ImGui descriptor we made for it.
		streamer_.setOnEvict([this](const std::string& name) {
			auto it = imTex_.find(name);
			if (it != imTex_.end()) {
				ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(it->second));
				imTex_.erase(it);
			}
		});
	}

	LegoBrowserPanel::~LegoBrowserPanel()
	{
		streamer_.setOnEvict(nullptr);   // stop callbacks into a dying panel
		for (auto& kv : imTex_)
			ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(kv.second));
	}

	LegoBrowserPanel::TexId LegoBrowserPanel::imTextureFor(const std::string& name,
	                                                       const BGLTextureStreamer::ResidentTexture& t)
	{
		auto it = imTex_.find(name);
		if (it != imTex_.end()) return it->second;
		VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(t.view, t.layout);
		TexId id = reinterpret_cast<TexId>(ds);
		imTex_.emplace(name, id);
		return id;
	}

	void LegoBrowserPanel::draw(bool* open)
	{
		if (!ImGui::Begin("Lego Parts", open)) { ImGui::End(); return; }

		// --- controls: filter + sort ---------------------------------------------------
		ImGui::SetNextItemWidth(180.0f);
		ImGui::InputTextWithHint("##filter", "filter name/title", filter_, sizeof(filter_));
		ImGui::SameLine();
		if (ImGui::RadioButton("name", sortMode_ == 0))  { sortMode_ = 0; library_.sortByName(); }
		ImGui::SameLine();
		if (ImGui::RadioButton("title", sortMode_ == 1)) { sortMode_ = 1; library_.sortByTitle(); }
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::SliderFloat("size", &thumbPx_, 32.0f, 128.0f, "%.0f");

		// --- filtered index list -------------------------------------------------------
		const auto& entries = library_.entries();
		const std::string f = toLower(filter_);
		filtered_.clear();
		filtered_.reserve(entries.size());
		for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
			if (f.empty()
			    || toLower(entries[i].name).find(f)  != std::string::npos
			    || toLower(entries[i].title).find(f) != std::string::npos)
				filtered_.push_back(i);
		}

		ImGui::Text("%d / %zu parts   |   resident %zu/%zu%s",
		            (int)filtered_.size(), entries.size(),
		            streamer_.residentCount(), streamer_.residentCap(),
		            selected_.empty() ? "" : ("   |   selected: " + selected_).c_str());
		ImGui::Separator();

		// --- thumbnail grid (only visible rows request/keep textures) ------------------
		const float cell = thumbPx_ + ImGui::GetStyle().ItemSpacing.x + 8.0f;
		const float avail = ImGui::GetContentRegionAvail().x;
		const int cols = std::max(1, static_cast<int>(avail / cell));
		const int rows = (static_cast<int>(filtered_.size()) + cols - 1) / cols;
		const ImVec2 sz(thumbPx_, thumbPx_);
		const float rowH = thumbPx_ + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

		ImGui::BeginChild("grid", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
		ImGuiListClipper clipper;
		clipper.Begin(rows, rowH);
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				for (int c = 0; c < cols; ++c) {
					const int fi = row * cols + c;
					if (fi >= static_cast<int>(filtered_.size())) break;
					const ldraw::PartCatalogEntry& e = entries[filtered_[fi]];

					if (c > 0) ImGui::SameLine();
					ImGui::PushID(fi);
					ImGui::BeginGroup();

					// Only on-screen cells reach here (clipper) -> lazy request; get() keeps it hot.
					streamer_.request(e.name, e.thumbPath);
					const BGLTextureStreamer::ResidentTexture* t = streamer_.get(e.name);

					const bool isSel = (selected_ == e.name);
					const ImVec4 tint = isSel ? SELECTION_TINT : ImVec4(1, 1, 1, 1);
					bool clicked = false;
					if (t) {
						clicked = ImGui::ImageButton("thumb", imTextureFor(e.name, *t), sz,
						                             ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
					} else {
						clicked = ImGui::Button("...", sz);   // placeholder while loading
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s\n%s", e.name.c_str(), e.title.c_str());
					if (clicked) {
						selected_ = e.name;
						if (onPick_) onPick_(e);
					}

					// name label, clipped to the cell width
					ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + thumbPx_);
					ImGui::TextUnformatted(e.name.c_str());
					ImGui::PopTextWrapPos();

					ImGui::EndGroup();
					ImGui::PopID();
				}
			}
		}
		clipper.End();
		ImGui::EndChild();

		ImGui::End();
	}

} // namespace bagel
