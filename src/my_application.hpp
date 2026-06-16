#pragma once
#include "bagel_application.hpp"
#include "bagel_material.hpp"

namespace bagel {

	class MyApplication : public Application {
	public:
		void OnSceneLoad() override;
		void OnUpdate(float dt) override;
		void OnDrawGui() override;   // draws the Maps panel (build / save / load)

	private:
		// Scene content (each builds its own lights so it stands alone after a clear)
		void placeCubes();
		void placePurpleCube();
		void createLights();   // point lights — common to every scene
		void buildHierarchyStack();
		void loadSponza();
		void loadDragon();
		void loadMonkeyBone();   // skinned/bone-animated test model

		// Map pipeline
		void buildScene(int index);     // clear + build one of the 3 scenes live
		void saveCurrentMap();          // serialize current registry to maps/<name>.bmap
		void loadMap(int index);        // load from disk (if it exists) + rehydrate
		void rehydrateScene();          // rebuild transient GPU/material state after load
		static const char* mapName(int index);
		std::string        mapPath(int index) const;

		int currentMap = 1;             // active slot (set by build / successful load)
		entt::entity hierarchyRoot = entt::null;
		float stackAngle = 0.0f;
	};

} // namespace bagel
