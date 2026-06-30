#pragma once
#include "application/bagel_application.hpp"
#include "bagel_material.hpp"
#include "game/planet_terrain.hpp"   // geodesic-CDLOD planet (workstream G)
#include "components/planet.hpp"
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
		void loadPlanet();       // dynamically-subdividing icosphere (solid mesh)

		// Planet (geodesic-CDLOD) demo state. The tree is the authoritative terrain;
		// the ModelComponent's buffers are rebuilt from the LOD cut as the camera moves.
		//void updatePlanet(const glm::vec3& camWorldPos); // called each frame from OnUpdate
		//void rebuildPlanetMesh(const glm::vec3& camWorldPos);
		//std::unique_ptr<planet::PlanetTerrain> planetTerrain;
		//entt::entity planetEntity = entt::null;
		//glm::vec3 planetLastRebuildCam{ 1e9f };

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

		PlanetComponentSystem pcs;
	};

} // namespace bagel
