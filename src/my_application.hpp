#pragma once
#include "bagel_application.hpp"
#include "bagel_material.hpp"

namespace bagel {

	class MyApplication : public Application {
	public:
		void OnSceneLoad() override;
		void OnUpdate(float dt) override;
		void OnDrawGui() override;   // draws the Maps panel (build / save / load)
		std::string consoleLoadMap(const std::string& name) override; // "map <name>" console command

	private:
		// Scene content (each builds its own lights so it stands alone after a clear)
		void placeCubes();
		void placePurpleCube();
		void createLights();   // point lights — common to every scene
		void buildHierarchyStack();
		void loadSponza();
		void loadDragon();
		void loadMonkeyBone();   // skinned/bone-animated test model
		void loadIKLeg();

		// Map pipeline
		void buildScene(int index);     // clear + build one of the 3 scenes live
		void saveCurrentMap();          // serialize current registry to maps/<name>.bmap
		void loadMap(int index);        // load from disk (if it exists) + rehydrate
		bool loadMapFromPath(const std::string& path, const std::string& name); // shared load+rehydrate
		void rehydrateScene();          // rebuild transient GPU/material state after load
		static const char* mapName(int index);
		std::string        mapPath(int index) const;
		std::string        mapPath(const std::string& name) const;

		std::string currentMapName = "sponza"; // active map name (set by build / successful load)
		entt::entity hierarchyRoot = entt::null;
		float stackAngle = 0.0f;
	};

} // namespace bagel
