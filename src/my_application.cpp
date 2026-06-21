#include "my_application.hpp"
#include "bagel_hierachy.hpp"
#include "physics/bagel_jolt.hpp"
#include "map/bagel_map_io.hpp"
#include "bagel_util.hpp"
#include "bagel_imgui.hpp"   // ImGui + ConsoleApp (CONSOLE)
#include "model_loaders/bagel_model_loader.hpp" // BGLModel::Vertex (planet wire upload)

#include <iostream>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace bagel {

	// rehydrateModelType<T> + the scene rehydrate pass moved to map/bagel_map_io.{hpp,cpp}
	// (Map::rehydrate). Called from loadMapFromPath after Map::load.
	MyApplication::MyApplication() : Application(), pcs(bglDevice,registry)
	{

	}
	void MyApplication::OnSceneLoad()
	{
		buildScene(6); // start on the geodesic-CDLOD planet (wireframe)
	}

	void MyApplication::OnUpdate(float dt)
	{
		pcs.update(cameraWorldPos); // rebuild the planet LOD cut if the camera moved
		if (hierarchyRoot == entt::null)
			return;
		stackAngle += dt;

		auto& tc = registry.get<TransformComponent>(hierarchyRoot);
		tc.setTranslation({
			8.0f * cosf(stackAngle * 0.4f),
			1.0f + 0.5f * sinf(stackAngle * 0.7f),
			8.0f * sinf(stackAngle * 0.4f)
		});
		tc.setRotation({
			sinf(stackAngle * 0.5f) * 0.3f,
			stackAngle * 0.8f,
			cosf(stackAngle * 0.3f) * 0.2f
		});
	}

	// ---- Map panel + pipeline ----------------------------------------------

	const char* MyApplication::mapName(int index)
	{
		switch (index) {
		case 0:  return "cube_field";
		case 1:  return "sponza";
		case 2:  return "hierarchy";
		case 3:  return "dragon";
		case 4:  return "monkey";
		case 6:  return "planet";
		default: return "map";
		}
	}

	std::string MyApplication::mapPath(int index) const
	{
		return util::enginePath((std::string("/maps/") + mapName(index) + ".bmap").c_str());
	}

	std::string MyApplication::mapPath(const std::string& name) const
	{
		return util::enginePath((std::string("/maps/") + name + ".bmap").c_str());
	}

	void MyApplication::buildScene(int index)
	{
		// Drop the current scene: waits for the GPU, tears down physics bodies, clears ECS.
		Map::unload(registry);
		// Reset the skin-table allocator (GPU is idle after unload); the new scene's models
		// reallocate their blocks from scratch.
		materialManager->clearSkinTable();
		hierarchyRoot = entt::null;
		
		currentMapName = mapName(index);

		switch (index) {
		case 0: createLights();              placeCubes();         break;
		case 1: createLights();              loadSponza();         break;
		case 2: createLights();              buildHierarchyStack(); break;
		case 3: createLights();              loadDragon();         break;
		case 4: createLights();              loadMonkeyBone();     break;
		case 5: createLights();              loadIKLeg();     break;
		case 6: createLights();              loadPlanet();         break;
		default: break;
		}
		CONSOLE->Log("Map", std::string("Built scene '") + mapName(index) + "'");
	}

	void MyApplication::saveCurrentMap()
	{
		const std::string path = mapPath(currentMapName);
		const bool ok = Map::save(registry, path);
		CONSOLE->Log("Map", (ok ? "Saved " : "FAILED to save ") + path);
	}

	// Shared load path used by the Maps panel and the "map <name>" console command. Assumes the
	// file exists; unloads the current scene, restores persistent data, rebuilds transient GPU state,
	// and marks `name` active. Returns false if Map::load itself fails.
	bool MyApplication::loadMapFromPath(const std::string& path, const std::string& name)
	{
		if (!Map::load(registry, path)) return false;  // unloads current scene + restores persistent data
		// Map::load unloaded the old scene (GPU idle); reset the skin-table allocator before
		// rehydrate rebuilds the loaded models' blocks.
		materialManager->clearSkinTable();
		// rebuild transient GPU/material/physics state (moved to map/bagel_map_io.*)
		Map::rehydrate(registry, bglDevice, *materialManager, *skinManager);
		hierarchyRoot = entt::null;              // loaded hierarchy is static (no live root)
		currentMapName = name;
		return true;
	}

	void MyApplication::loadMap(int index)
	{
		const std::string path = mapPath(index);
		if (!Map::exists(path)) {
			CONSOLE->Log("Map", "No map file (save it first): " + path);
			return;
		}
		if (!loadMapFromPath(path, mapName(index))) {
			CONSOLE->Log("Map", "FAILED to load " + path);
			return;
		}
		CONSOLE->Log("Map", std::string("Loaded map '") + mapName(index) + "'");
	}

	// "map <name>" console command: load /maps/<name>.bmap by name. Returns a status message.
	std::string MyApplication::consoleLoadMap(const std::string& name)
	{
		if (name.empty()) return "map <name>: load /maps/<name>.bmap";
		const std::string path = mapPath(name);
		if (!Map::exists(path))               return "[error] map: not found: " + path;
		if (!loadMapFromPath(path, name))     return "[error] map: failed to load " + path;
		return "Loaded map '" + name + "'";
	}

	void MyApplication::OnDrawGui()
	{
		ImGui::Begin("Maps");

		ImGui::TextUnformatted("Build (live):");
		if (ImGui::Button("Cube Field")) buildScene(0);
		ImGui::SameLine();
		if (ImGui::Button("Sponza"))     buildScene(1);
		ImGui::SameLine();
		if (ImGui::Button("Hierarchy"))  buildScene(2);
		ImGui::SameLine();
		if (ImGui::Button("Dragon"))     buildScene(3);
		ImGui::SameLine();
		if (ImGui::Button("Monkey"))     buildScene(4);
		ImGui::SameLine();
		if (ImGui::Button("IKBone"))     buildScene(5);
		ImGui::SameLine();
		if (ImGui::Button("Planet"))     buildScene(6);

		ImGui::Separator();
		if (ImGui::Button("Save as map")) saveCurrentMap();
		ImGui::SameLine();
		ImGui::Text("(active: %s)", currentMapName.c_str());

		ImGui::Separator();
		ImGui::TextUnformatted("Load (from disk):");
		for (int i = 0; i < 6; ++i) {
			const bool onDisk = Map::exists(mapPath(i));
			std::string label = std::string("Load ") + mapName(i) + (onDisk ? "" : " (none)");
			if (ImGui::Button(label.c_str())) loadMap(i);
		}

		ImGui::End();

		DrawRegistryPanel(registry);
	}

	// ---- scene content ------------------------------------------------------

	void MyApplication::placeCubes()
	{
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);

		// Texture sources are recorded on each generated cube so the brick look
		// survives a save/load round trip (generated models carry no asset material).
		MaterialSource bricksSrc{
			"/materials/Bricks089_1K-PNG_Color.png",
			"/materials/Bricks089_1K-PNG_NormalGL.png",
			"/materials/Bricks089_1K-PNG_Roughness.png",
			"" };

		struct CubeDef { glm::vec3 pos; glm::vec3 scale; };
		CubeDef defs[] = {
			{{ 2.0f,  0.0f,  0.0f}, {0.4f, 0.4f, 0.4f}},
			{{-2.0f,  0.0f,  0.0f}, {0.4f, 0.4f, 0.4f}},
			{{ 0.0f,  0.0f,  2.0f}, {0.4f, 0.4f, 0.4f}},
			{{ 0.0f,  0.0f, -2.0f}, {0.4f, 0.4f, 0.4f}},
			{{ 1.5f,  0.6f,  1.5f}, {0.3f, 0.3f, 0.3f}},
			{{-1.5f,  0.6f,  1.5f}, {0.3f, 0.3f, 0.3f}},
			{{ 1.5f,  0.6f, -1.5f}, {0.3f, 0.3f, 0.3f}},
			{{-1.5f,  0.6f, -1.5f}, {0.3f, 0.3f, 0.3f}},
		};

		for (auto& def : defs) {
			auto entity = registry.create();
			auto& tfc = registry.emplace<TransformComponent>(entity);
			tfc.setTranslation(def.pos);
			tfc.setScale(def.scale);
			auto& model = modelBuilder->buildComponent<ModelComponent>(entity, "/models/cube.obj", ComponentBuildMode::FACES);
			// Record the material source so the brick look survives a save/load round trip.
			model.setMaterialSource(0, bricksSrc);
		}

		delete modelBuilder;
	}
	void MyApplication::placePurpleCube()
	{
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);

		MaterialSource purpleSrc{
			"/materials/purple_albedo.png",
			"", "",
			"/materials/purple_emission.png" };

		auto entity = registry.create();
		auto& tfc = registry.emplace<TransformComponent>(entity);
		tfc.setTranslation({ 0.0f, 0.0f, 0.0f });
		tfc.setScale({ 0.3f, 0.3f, 0.3f });
		ModelLoadSettings settings{};
		auto &model = modelBuilder->buildComponent<ModelComponent>(entity, "/models/cube.obj", settings);
		model.setMaterialSource(0, purpleSrc);

		delete modelBuilder;
	}
	void MyApplication::buildHierarchyStack()
	{
		const int   COUNT        = 1000;
		const float cubeScale    = 0.2f;
		const float localYOffset = 1.5f;   // in parent-local units; world offset = cubeScale * 1.5 = 0.3
		const float twistPerLevel = glm::radians(8.0f);

		auto* builder = new ModelComponentBuilder(bglDevice, registry);
		HierachySystem hs(registry);

		ModelLoadSettings settings{};
		settings.scaleVec = { 1.0f, 1.0f, 1.0f };

		entt::entity prev = entt::null;
		for (int i = 0; i < COUNT; i++) {
			entt::entity e = registry.create();
			auto& tc = registry.emplace<TransformComponent>(e);
			tc.setScale({ cubeScale, cubeScale, cubeScale });
			builder->buildComponent<ModelComponent>(e, "/models/cube.obj", settings);

			if (i == 0) {
				tc.setTranslation({ 8.0f, 0.0f, 0.0f });
				hierarchyRoot = e;
			} else {
				hs.CreateHierachy(prev, e);
				auto& hier = registry.get<TransformHierachyComponent>(e);
				hier.localTranslation = { 0.0f, localYOffset, 0.0f };
				hier.localRotation    = { 0.0f, twistPerLevel, 0.0f };
				hier.localScale       = { 1.0f, 1.0f, 1.0f };
			}
			prev = e;
		}

		delete builder;
	}
	void MyApplication::createLights()
	{
		std::vector<glm::vec3> lightColors{
			 {1.f, .1f, .1f},
			 {.1f, .1f, 1.f},
			 {.1f, 1.f, .1f},
			 {1.f, 1.f, .1f},
			 {.1f, 1.f, 1.f},
			 {1.f, 1.f, 1.f}
		};
		for (int i = 0; i < (int)lightColors.size(); i++) {
			auto rot = glm::rotate(
				glm::mat4(1.0f),
				(i * glm::two_pi<float>() / lightColors.size()),
				{ 0.f,-1.f,0.f });
			const auto entity = registry.create();
			registry.emplace<TransformComponent>(entity, (rot * glm::vec4(glm::vec3{ 3.f,1.0f,0.0f }, 1.0f)));
			registry.emplace<InfoComponent>(entity);
			auto& light = registry.emplace<PointLightComponent>(entity);
			light.color = glm::vec4(lightColors[i], 1.0f);
			light.lux = 800.0f;
		}
		const auto entity = registry.create();
		auto& sun = registry.emplace<DirectionalLightComponent>(entity);
		sun.color = { 0.6f, 0.6f, 0.2f, 1.0f };
		sun.rotation = { -64.0f, 30.0f, 0.0f };
		sun.shadowBiasMin = 0.0f;
		sun.shadowBiasSlope = 0.0f;
	}
	void MyApplication::loadSponza()
	{
		auto* builder = new ModelComponentBuilder(bglDevice, registry);
		builder->setTextureLoader(&materialManager->getTextureLoader());
		builder->setMaterialManager(materialManager.get());
		builder->setSkinManager(skinManager.get());

		auto entity = registry.create();
		auto& tc = registry.emplace<TransformComponent>(entity);
		tc.setTranslation({ 0.0f, 0.0f, 0.0f });
		tc.setScale({ 0.01f, 0.01f, 0.01f });

		ModelLoadSettings settings{};
		// Sponza is large: keep each source mesh's solid geometry as its own submesh
		// (instead of merging into one) so per-submesh frustum culling can skip
		// off-screen chunks. Set to true to merge back into a single opaque submesh.
		settings.mergeSolidSubmeshes = false;
		builder->buildComponent<ModelComponent>(entity, "/models/sponza/Sponza.gltf", settings);

		delete builder;
	}
	void MyApplication::loadDragon()
	{
		auto* builder = new ModelComponentBuilder(bglDevice, registry);
		builder->setTextureLoader(&materialManager->getTextureLoader());
		builder->setMaterialManager(materialManager.get());
		builder->setSkinManager(skinManager.get());

		// Text glTF with a base64-embedded buffer and solid-color (baseColorFactor) materials.
		// ~14x6x10 units; scale it down so it sits comfortably in view.
		auto entity = registry.create();
		auto& tc = registry.emplace<TransformComponent>(entity);
		tc.setTranslation({ 0.0f, 0.0f, 0.0f });
		tc.setScale({ 0.3f, 0.3f, 0.3f });

		ModelLoadSettings settings{};
		builder->buildComponent<ModelComponent>(entity, "/models/chinesedragon.gltf", settings);

		delete builder;
	}
	void MyApplication::loadMonkeyBone()
	{
		// Skinned / bone-animated test model: 1 skin (5 joints), 2 clips (action1, action2).
		// The builder uploads its skin influences + baked palette and attaches an
		// AnimationComponent, so it renders through the SkinnedGBufferRenderSystem.
		auto* builder = new ModelComponentBuilder(bglDevice, registry);
		builder->setTextureLoader(&materialManager->getTextureLoader());
		builder->setMaterialManager(materialManager.get());
		builder->setSkinManager(skinManager.get());

		auto entity = registry.create();
		auto& tc = registry.emplace<TransformComponent>(entity);
		tc.setTranslation({ 0.0f, 0.0f, 0.0f });
		tc.setScale({ 1.0f, 1.0f, 1.0f });

		ModelLoadSettings settings{};
		builder->buildComponent<ModelComponent>(entity, "/models/monkey_bone_anim/monkeybone.glb", settings);

		delete builder;
	}
	void MyApplication::loadIKLeg()
	{
		// Skinned / bone-animated test model: 1 skin (5 joints), 2 clips (action1, action2).
		// The builder uploads its skin influences + baked palette and attaches an
		// AnimationComponent, so it renders through the SkinnedGBufferRenderSystem.
		auto* builder = new ModelComponentBuilder(bglDevice, registry);
		builder->setTextureLoader(&materialManager->getTextureLoader());
		builder->setMaterialManager(materialManager.get());
		builder->setSkinManager(skinManager.get());

		auto entity = registry.create();
		auto& tc = registry.emplace<TransformComponent>(entity);
		tc.setTranslation({ 0.0f, 0.0f, 0.0f });
		tc.setScale({ 1.0f, 1.0f, 1.0f });

		ModelLoadSettings settings{};
		builder->buildComponent<ModelComponent>(entity, "/models/ikleg/ikbone.glb", settings);

		delete builder;
	}
	void MyApplication::loadPlanet()
	{
		// Planet: radius 64, tessellation ranging from a uniform level-2 floor (far side)
		// to level 8 near the camera.
		planet::TerrainConfig cfg{};
		cfg.radius      = 64.0f;
		cfg.minLevel    = 2;
		cfg.maxLevel    = 8;
		cfg.splitFactor = 2.5f;
		pcs.createPlanet({0, 0, 0}, cfg);
		// Default free-fly spawn ({0,-3,0}) is inside the radius-64 body — frame it from
		// outside (~2.5 radii out, within the 300-unit far plane).
		setSpawnCameraPos({ 0.0f, 0.0f, cfg.radius * 2.5f });
		CONSOLE->Log("Planet", "Geodesic-CDLOD icosphere — orbit/zoom to watch it subdivide.");
	}

} // namespace bagel

int main() {
	bagel::MyApplication app{};
	try {
		app.run();
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
	return 0;
}
