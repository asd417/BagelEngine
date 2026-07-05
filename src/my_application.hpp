#pragma once
#include "application/bagel_application.hpp"
#include "bagel_material.hpp"
#include "bagel_texture_streamer.hpp"                 // async thumbnail streamer (engine-level)
#include "lego/part_catalog.hpp"                      // placeable-part catalog
#include "lego/lego_browser_panel.hpp"                // ImGui part browser
#include "model_loaders/model_load_settings.hpp" // ModelLoadSettings (loadModel default arg)
#include <memory>

namespace bagel {

	class MyApplication : public Application {
	public:
		MyApplication();
		void OnSceneLoad() override;
		void OnUpdate(float dt) override;
		void OnDrawGui() override;   // draws the Maps panel (build / save / load)
		std::string consoleLoadMap(const std::string& name) override; // "map <name>" console command

	private:
		// Scene content (each builds its own lights so it stands alone after a clear)
		void placeCubes();
		void createLights();   // point lights — common to every scene
		void createDirectionalLight();
		void buildHierarchyStack();
		// One entity at origin from a single model asset (gltf/glb/obj). Shared by every
		// single-model scene below; settings carries per-model tweaks (e.g. submesh merge).
		void loadModel(const char* path, float scale, ModelLoadSettings settings = {});
		void loadSponza();
		void loadDragon();
		void loadMonkeyBone();   // skinned/bone-animated test model
		void loadIKLeg();
		void loadLegoBrick();    // single LDraw brick (3001.dat) — LDrawModelLoader smoke test
		// Spawn one LDraw part as a new entity at `pos` (studs up) with its connectors + collider.
		// Shared by the smoke test and the part-browser placement. Returns the new entity.
		entt::entity spawnLegoPart(const std::string& partName, const glm::vec3& pos);
		// NOTE: the geodesic-CDLOD planet (PlanetComponent / PlanetComponentSystem) is
		// mid-refactor and not wired into the scene list yet. The render system stays
		// registered in Application, but no planet is built or exposed in the GUI.

		// Map pipeline
		void buildScene(int index);     // clear + build one of the 3 scenes live
		void saveCurrentMap();          // serialize current registry to maps/<name>.bmap
		void loadMap(int index);        // load from disk (if it exists) + rehydrate
		bool loadMapFromPath(const std::string& path, const std::string& name); // shared load+rehydrate
		void logDescriptorUsage();      // print bindless slot usage after a scene loads
		static const char* mapName(int index);
		std::string        mapPath(int index) const;
		std::string        mapPath(const std::string& name) const;

		std::string currentMapName = "sponza"; // active map name (set by build / successful load)
		entt::entity hierarchyRoot = entt::null;
		float stackAngle = 0.0f;

		// Part picker. Declaration order matters for teardown: the streamer must outlive the
		// panel (the panel frees ImGui descriptors that reference the streamer's images), so it
		// is declared FIRST and thus destroyed LAST.
		ldraw::PartCatalog                  partCatalog_;
		std::unique_ptr<BGLTextureStreamer> thumbnailStreamer_;
		std::unique_ptr<LegoBrowserPanel>   legoBrowser_;
		bool                                showLegoBrowser_ = true;
	};

} // namespace bagel
