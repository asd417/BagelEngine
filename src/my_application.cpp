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
		pcs.update(cameraWorldPos, materialManager->getTextureLoader()); // rebuild LOD cut + push paint edits
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
		logDescriptorUsage();
	}

	// Report how much of the bindless descriptor capacity the just-loaded scene consumes.
	void MyApplication::logDescriptorUsage()
	{
		const uint32_t cap  = descriptorManager->descriptorCapacity();
		const uint32_t tex  = descriptorManager->textureSlotsUsed();
		const uint32_t buf  = descriptorManager->bufferSlotsUsed();
		CONSOLE->Log("Descriptors",
			"textures " + std::to_string(tex) + "/" + std::to_string(cap) +
			", buffers " + std::to_string(buf) + "/" + std::to_string(cap));
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
		// Planets aren't model-type entities, so rebuild their transient state here from the
		// loaded recipe: reconstruct the terrain tree from cfg, re-bind + fold in the painted
		// cube-map, re-upload the 6 R16 faces, and let the next pcs.update() rebuild the mesh.
		for (auto [e, pc] : registry.view<PlanetComponent>().each()) {
			pc.terrain = std::make_unique<planet::PlanetTerrain>(pc.cfg);
			pc.terrain->bindPaint(pc.paint.data());
			pc.terrain->recomputeRadii();
			pcs.allocatePaintTextures(e, pc, materialManager->getTextureLoader());
			pcs.setupOceanMaterial(registry.get<ModelComponent>(e), *materialManager); // re-reserve ocean skin slot
			pc.planetLastRebuildCam = glm::vec3(1e9f); // force LOD mesh rebuild next frame
		}
		hierarchyRoot = entt::null;              // loaded hierarchy is static (no live root)
		currentMapName = name;
		logDescriptorUsage();
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

		// Live LOD + procedural-height control for any planet in the scene. Editing any
		// value forces a fresh LOD cut next frame via the planetLastRebuildCam sentinel.
		for (auto [ent, pc] : registry.view<PlanetComponent>().each()) {
			if (!pc.terrain) continue;
			const auto& cfg = pc.terrain->config();
			ImGui::Separator();
			ImGui::TextUnformatted("Planet LOD");
			// "px size": higher splitFactor subdivides from farther away -> smaller
			// on-screen triangles. Logarithmic so the wide range stays usable.
			float sf = cfg.splitFactor;
			if (ImGui::SliderFloat("Split distance", &sf, 0.1f, 512.0f, "%.2f x edge",
			                       ImGuiSliderFlags_Logarithmic)) {
				pc.terrain->setSplitFactor(sf);
				pc.planetLastRebuildCam = glm::vec3(1e9f); // sentinel -> force rebuild
			}

			ImGui::TextUnformatted("Planet Noise");
			float    amp     = cfg.noiseAmplitude;
			float    freq    = cfg.noiseFrequency;
			int      oct     = cfg.noiseOctaves;
			float    lac     = cfg.noiseLacunarity;
			float    gain    = cfg.noiseGain;
			float    sea     = cfg.sealevel;
			int      seed    = static_cast<int>(cfg.seed);
			bool     changed = false;
			changed |= ImGui::SliderFloat("Amplitude", &amp, 0.0f, 64.0f, "%.2f");
			changed |= ImGui::SliderFloat("Scaling",   &freq, 0.1f, 64.0f, "%.2f",
			                              ImGuiSliderFlags_Logarithmic);
			changed |= ImGui::SliderInt  ("Detailing", &oct, 1, 10);
			changed |= ImGui::SliderFloat("Lacunarity", &lac, 1.0f, 4.0f, "%.2f");
			changed |= ImGui::SliderFloat("Gain", &gain, 0.0f, 1.0f, "%.2f");
			// Sea level caps surface radius; range = the planet's natural height band.
			changed |= ImGui::SliderFloat("Sea level", &sea, cfg.radius - amp, cfg.radius + amp, "%.2f");
			changed |= ImGui::InputInt   ("Seed", &seed);
			if (changed) {
				if (oct < 1) oct = 1;
				if (seed < 0) seed = 0;
				pc.terrain->setNoise(amp, freq, oct, lac, gain, sea, static_cast<uint32_t>(seed));
				pc.planetLastRebuildCam = glm::vec3(1e9f); // sentinel -> force rebuild
			}

			// Height painting (B also toggles; LMB-drag on the surface paints).
			ImGui::TextUnformatted("Planet Paint");
			auto& paint = getPlanetPaint();
			bool paintOn = paint.editModeOn();
			if (ImGui::Checkbox("Paint mode (B)", &paintOn)) paint.setEditMode(paintOn);
			ImGui::SliderFloat("Brush radius", &paint.brushRadiusDeg, 1.0f, 45.0f, "%.1f deg");
			ImGui::SliderFloat("Brush strength", &paint.strength, 0.05f, 8.0f, "%.2f");
			ImGui::SliderInt("Brush detail", &paint.targetLevel, 2, cfg.maxLevel);
			ImGui::Checkbox("Carve (lower)", &paint.lower);
		}

		ImGui::End();

		DrawRegistryPanel(registry);
	}

	// ---- scene content ------------------------------------------------------

	void MyApplication::placeCubes()
	{
		ModelComponentBuilder modelBuilder(bglDevice, registry);

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
			auto& model = modelBuilder.buildComponent<ModelComponent>(entity, "/models/cube.obj", ComponentBuildMode::FACES);
			// Record the material source so the brick look survives a save/load round trip.
			model.setMaterialSource(0, bricksSrc);
		}
	}
	void MyApplication::buildHierarchyStack()
	{
		const int   COUNT        = 1000;
		const float cubeScale    = 0.2f;
		const float localYOffset = 1.5f;   // in parent-local units; world offset = cubeScale * 1.5 = 0.3
		const float twistPerLevel = glm::radians(8.0f);

		ModelComponentBuilder builder(bglDevice, registry);
		HierachySystem hs(registry);

		ModelLoadSettings settings{};
		settings.scaleVec = { 1.0f, 1.0f, 1.0f };

		entt::entity prev = entt::null;
		for (int i = 0; i < COUNT; i++) {
			entt::entity e = registry.create();
			auto& tc = registry.emplace<TransformComponent>(e);
			tc.setScale({ cubeScale, cubeScale, cubeScale });
			builder.buildComponent<ModelComponent>(e, "/models/cube.obj", settings);

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
	// One textured/skinned entity at origin. Skinned models (glb with JOINTS_0/WEIGHTS_0) get
	// their skin influences + baked palette uploaded and an AnimationComponent attached by the
	// builder, so they render through the SkinnedGBufferRenderSystem automatically.
	void MyApplication::loadModel(const char* path, float scale, ModelLoadSettings settings)
	{
		ModelComponentBuilder builder(bglDevice, registry);
		builder.setTextureLoader(&materialManager->getTextureLoader());
		builder.setMaterialManager(materialManager.get());
		builder.setSkinManager(skinManager.get());

		auto entity = registry.create();
		registry.emplace<TransformComponent>(entity).setScale({ scale, scale, scale });
		builder.buildComponent<ModelComponent>(entity, path, settings);
	}
	void MyApplication::loadSponza()
	{
		ModelLoadSettings settings{};
		// Sponza is large: keep each source mesh's solid geometry as its own submesh
		// (instead of merging into one) so per-submesh frustum culling can skip
		// off-screen chunks. Set to true to merge back into a single opaque submesh.
		settings.mergeSolidSubmeshes = false;
		loadModel("/models/sponza/Sponza.gltf", 0.01f, settings);
	}
	// Text glTF with a base64-embedded buffer and solid-color (baseColorFactor) materials.
	// ~14x6x10 units; scaled down so it sits comfortably in view.
	void MyApplication::loadDragon()    { loadModel("/models/chinesedragon.gltf", 0.3f); }
	void MyApplication::loadMonkeyBone(){ loadModel("/models/monkey_bone_anim/monkeybone.glb", 1.0f); }
	void MyApplication::loadIKLeg()     { loadModel("/models/ikleg/ikbone.glb", 1.0f); }
	void MyApplication::loadPlanet()
	{
		// Planet: radius 64, tessellation ranging from a uniform level-2 floor (far side)
		// to level 8 near the camera.
		planet::TerrainConfig cfg{};
		cfg.radius      = 64.0f;
		cfg.minLevel    = 2;
		cfg.maxLevel    = 8;
		cfg.splitFactor = 5.0f;
		cfg.noiseAmplitude = 11.0f;
		cfg.noiseFrequency = 2.0f;  // "scaling"
		cfg.noiseOctaves   = 3;     // "detailing"
		cfg.sealevel       = 64.0f; // = base radius: floor at sea level, full noise band pokes above
		pcs.createPlanet({0, 0, 0}, cfg, *materialManager);
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
