#pragma once
#include "application/bagel_application.hpp"
#include "bagel_material.hpp"
#include "model/model_loaders/model_load_settings.hpp" // ModelLoadSettings (loadModel default arg)
#include <memory>

namespace bagel {

	class MyApplication : public Application {
	public:
		MyApplication();
		void OnSceneLoad() override;
		void OnUpdate(BGLCamera& camera, float dt) override;
		void OnDrawGui() override;   // draws the Maps panel (build / save / load)
		std::string consoleLoadMap(const std::string& name) override; // "map <name>" console command
		std::string consoleLoadTextMap(const std::string& name) override; // "textmap <name>" console command

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
		void loadSponzaStress(); // 1000 Sponzas on a grid — draw-call / culling stress test
		void loadSponzaInstanced(); // same grid, but ONE entity via TransformArrayComponent (instanced)
		void loadDragon();
		void loadMonkeyBone();   // skinned/bone-animated test model
		void loadIKLeg();
		// NOTE: the geodesic-CDLOD planet (PlanetComponent / PlanetComponentSystem) is
		// mid-refactor and not wired into the scene list yet. The render system stays
		// registered in Application, but no planet is built or exposed in the GUI.

		bool mouseLeftPrev = false; // edge-detect left-click for entity picking

		// Map pipeline
		void buildScene(int index);     // clear + build one of the 3 scenes live
		void saveCurrentMap();          // serialize current registry to maps/<name>.bmap
		void loadMap(int index);        // load from disk (if it exists) + rehydrate
		bool loadMapFromPath(const std::string& path, const std::string& name); // shared load+rehydrate
		bool loadTextMap(const std::string& name); // build /maps/<name>.yaml (human-readable static map) live
		void logDescriptorUsage();      // print bindless slot usage after a scene loads
		static const char* mapName(int index);
		std::string        mapPath(int index) const;
		std::string        mapPath(const std::string& name) const;

		std::string currentMapName = "sponza"; // active map name (set by build / successful load)
		entt::entity hierarchyRoot = entt::null;
		float stackAngle = 0.0f;
	};

} // namespace bagel
