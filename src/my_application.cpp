#include "my_application.hpp"
#include "bagel_hierachy.hpp"
#include "physics/bagel_jolt.hpp"
#include "map/bagel_map_io.hpp"
#include "bagel_util.hpp"
#include "bagel_imgui.hpp"						// ImGui + ConsoleApp (CONSOLE)
#include "model_loaders/bagel_model_loader.hpp" // BGLModel::Vertex (planet wire upload)
#include "lego/components/lego_connection_component.hpp"	// LegoConnectionComponent (gizmo markers)
#include "lego/lego_model_builder.hpp"					// LegoModelComponentBuilder (.dat rehydrate)

#include <iostream>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace bagel
{

	// rehydrateModelType<T> + the scene rehydrate pass moved to map/bagel_map_io.{hpp,cpp}
	// (Map::rehydrate). Called from loadMapFromPath after Map::load.
	MyApplication::MyApplication() : Application()
	{
		// Part picker: scan the placeable-part catalog and stand up the async thumbnail streamer
		// + browser panel. (bglDevice is fully constructed by the base Application ctor above.)
		const size_t nParts = partCatalog_.scan(util::enginePath(""));
		CONSOLE->Log("Lego", "Part catalog: " + std::to_string(nParts) + " placeable parts");
		thumbnailStreamer_ = std::make_unique<BGLTextureStreamer>(bglDevice, 512);
		legoBrowser_ = std::make_unique<LegoBrowserPanel>(partCatalog_, *thumbnailStreamer_);
		partSystem_ = std::make_unique<ldraw::PartSystem>(bglDevice, registry, *materialManager, *skinManager);
		legoBrowser_->setOnPick([this](const ldraw::PartCatalogEntry &e)
								{
									partSystem_->spawnLegoPart(e.name, glm::vec3(0.0f)); // place at world origin
								});
	}
	void MyApplication::OnSceneLoad()
	{
		buildScene(6); // TEMP: verify connection gizmo
	}

	void MyApplication::OnUpdate(BGLCamera &camera, float dt)
	{
		// Publish finished thumbnail uploads + retire evicted textures (once per frame, main thread).
		if (thumbnailStreamer_)
			thumbnailStreamer_->beginFrame();

		// Left-click brick picking: cast a ray from the cursor into the physics scene and select
		// the block under it (BGLJolt::PickEntity resolves group compounds down to the block).
		{
			GLFWwindow *win = bglWindow.getGLFWWindow();
			const bool mouseLeftDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
			if (mouseLeftDown && !mouseLeftPrev && !ImGui::GetIO().WantCaptureMouse)
			{
				VkExtent2D ext = bglRenderer.getExtent();
				double mx, my;
				glfwGetCursorPos(win, &mx, &my);
				Ray r = camera.mouseRayCast(ext.width, ext.height, mx, my);
				glm::vec3 hitPos;
				selectedEntity = BGLJolt::GetInstance()->PickEntity(r, &hitPos);
				if (registry.valid(selectedEntity))
				{
					CONSOLE->Log("Pick", "Selected entity " + std::to_string(static_cast<uint32_t>(entt::to_integral(selectedEntity))));
					auto &brick = registry.get<LegoConnectionComponent>(selectedEntity);
					
				}
				else
					CONSOLE->Log("Pick", "Nothing under cursor");
			}
			mouseLeftPrev = mouseLeftDown;
		}
	}

	// Build the app-owned overlay render system now that the swapchain render pass + descriptor
	// layouts exist (called once by Application::run before the frame loop).
	void MyApplication::OnRenderInit(VkRenderPass swapchainPass,
	                                 const std::vector<VkDescriptorSetLayout> &setLayouts)
	{
		legoConnectionRenderSystem_ = std::make_unique<LegoConnectionRenderSystem>(
			swapchainPass, setLayouts, bglDevice);
	}

	// Draw the LEGO connection markers in the swapchain pass (called each frame by Application::run
	// at the overlay stage, after the scene + engine gizmo, before ImGui).
	void MyApplication::OnSwapchainOverlay(FrameInfo &frameInfo)
	{
		if (legoConnectionRenderSystem_)
			legoConnectionRenderSystem_->render(frameInfo);
	}

	// ---- Map panel + pipeline ----------------------------------------------

	const char *MyApplication::mapName(int index)
	{
		switch (index)
		{
		case 0:
			return "cube_field";
		case 1:
			return "sponza";
		case 2:
			return "hierarchy";
		case 3:
			return "dragon";
		case 4:
			return "monkey";
		case 5:
			return "ikbone";
		case 6:
			return "lego";
		default:
			return "map";
		}
	}

	std::string MyApplication::mapPath(int index) const
	{
		return util::enginePath((std::string("/maps/") + mapName(index) + ".bmap").c_str());
	}

	std::string MyApplication::mapPath(const std::string &name) const
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

		switch (index)
		{
		case 0:
			createLights();
			createDirectionalLight();
			placeCubes();
			break;
		case 1:
			createLights();
			createDirectionalLight();
			loadSponza();
			break;
		case 2:
			createLights();
			createDirectionalLight();
			buildHierarchyStack();
			break;
		case 3:
			createLights();
			createDirectionalLight();
			loadDragon();
			break;
		case 4:
			createLights();
			createDirectionalLight();
			loadMonkeyBone();
			break;
		case 5:
			createLights();
			createDirectionalLight();
			loadIKLeg();
			break;
		case 6:
			// createLights();
			createDirectionalLight();
			loadLegoGround();
			loadLegoBrick();
			break;
		default:
			break;
		}
		CONSOLE->Log("Map", std::string("Built scene '") + mapName(index) + "'");
		logDescriptorUsage();
	}

	// Report how much of the bindless descriptor capacity the just-loaded scene consumes.
	void MyApplication::logDescriptorUsage()
	{
		const uint32_t cap = descriptorManager->descriptorCapacity();
		const uint32_t tex = descriptorManager->textureSlotsUsed();
		const uint32_t buf = descriptorManager->bufferSlotsUsed();
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
	bool MyApplication::loadMapFromPath(const std::string &path, const std::string &name)
	{
		if (!Map::load(registry, path))
			return false; // unloads current scene + restores persistent data
		// Map::load unloaded the old scene (GPU idle); reset the skin-table allocator before
		// rehydrate rebuilds the loaded models' blocks.
		materialManager->clearSkinTable();
		// rebuild transient GPU/material/physics state (moved to map/bagel_map_io.*). Pass a
		// LEGO-aware builder so saved ".dat" parts rebuild through LDrawModelLoader.
		LegoModelComponentBuilder rebuildBuilder(bglDevice, registry);
		Map::rehydrate(registry, rebuildBuilder, *materialManager, *skinManager);
		// NOTE: planet rehydration (PlanetComponent -> mesh rebuild) is disabled while the
		// geodesic-CDLOD terrain is mid-refactor. Maps containing planets will load their
		// recipe but not rebuild the terrain mesh.
		hierarchyRoot = entt::null; // loaded hierarchy is static (no live root)
		currentMapName = name;
		logDescriptorUsage();
		return true;
	}

	void MyApplication::loadMap(int index)
	{
		const std::string path = mapPath(index);
		if (!Map::exists(path))
		{
			CONSOLE->Log("Map", "No map file (save it first): " + path);
			return;
		}
		if (!loadMapFromPath(path, mapName(index)))
		{
			CONSOLE->Log("Map", "FAILED to load " + path);
			return;
		}
		CONSOLE->Log("Map", std::string("Loaded map '") + mapName(index) + "'");
	}

	// "map <name>" console command: load /maps/<name>.bmap by name. Returns a status message.
	std::string MyApplication::consoleLoadMap(const std::string &name)
	{
		if (name.empty())
			return "map <name>: load /maps/<name>.bmap";
		const std::string path = mapPath(name);
		if (!Map::exists(path))
			return "[error] map: not found: " + path;
		if (!loadMapFromPath(path, name))
			return "[error] map: failed to load " + path;
		return "Loaded map '" + name + "'";
	}

	void MyApplication::OnDrawGui()
	{
		ImGui::Begin("Maps");

		ImGui::TextUnformatted("Build (live):");
		if (ImGui::Button("Cube Field"))
			buildScene(0);
		ImGui::SameLine();
		if (ImGui::Button("Sponza"))
			buildScene(1);
		ImGui::SameLine();
		if (ImGui::Button("Hierarchy"))
			buildScene(2);
		ImGui::SameLine();
		if (ImGui::Button("Dragon"))
			buildScene(3);
		ImGui::SameLine();
		if (ImGui::Button("Monkey"))
			buildScene(4);
		ImGui::SameLine();
		if (ImGui::Button("IKBone"))
			buildScene(5);
		ImGui::SameLine();
		if (ImGui::Button("Lego"))
			buildScene(6);

		ImGui::SameLine();
		ImGui::Checkbox("Part Browser", &showLegoBrowser_);

		ImGui::Separator();
		if (ImGui::Button("Save as map"))
			saveCurrentMap();
		ImGui::SameLine();
		ImGui::Text("(active: %s)", currentMapName.c_str());

		ImGui::Separator();
		ImGui::TextUnformatted("Load (from disk):");
		for (int i = 0; i < 7; ++i)
		{
			const bool onDisk = Map::exists(mapPath(i));
			std::string label = std::string("Load ") + mapName(i) + (onDisk ? "" : " (none)");
			if (ImGui::Button(label.c_str()))
				loadMap(i);
		}

		// NOTE: the live planet LOD/noise controls are removed while the geodesic-CDLOD
		// terrain is mid-refactor (PlanetComponent no longer exposes a live terrain tree).

		ImGui::End();

		if (showLegoBrowser_ && legoBrowser_)
			legoBrowser_->draw(&showLegoBrowser_);

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
			""};

		struct CubeDef
		{
			glm::vec3 pos;
			glm::vec3 scale;
		};
		CubeDef defs[] = {
			{{2.0f, 0.0f, 0.0f}, {0.4f, 0.4f, 0.4f}},
			{{-2.0f, 0.0f, 0.0f}, {0.4f, 0.4f, 0.4f}},
			{{0.0f, 0.0f, 2.0f}, {0.4f, 0.4f, 0.4f}},
			{{0.0f, 0.0f, -2.0f}, {0.4f, 0.4f, 0.4f}},
			{{1.5f, 0.6f, 1.5f}, {0.3f, 0.3f, 0.3f}},
			{{-1.5f, 0.6f, 1.5f}, {0.3f, 0.3f, 0.3f}},
			{{1.5f, 0.6f, -1.5f}, {0.3f, 0.3f, 0.3f}},
			{{-1.5f, 0.6f, -1.5f}, {0.3f, 0.3f, 0.3f}},
		};

		for (auto &def : defs)
		{
			auto entity = registry.create();
			auto &tfc = registry.emplace<TransformComponent>(entity);
			tfc.setTranslation(def.pos);
			tfc.setScale(def.scale);
			auto &model = modelBuilder.buildComponent(entity, "/models/cube.obj", ComponentBuildMode::FACES);
			// Record the material source so the brick look survives a save/load round trip.
			model.setMaterialSource(0, bricksSrc);
		}
	}
	void MyApplication::buildHierarchyStack()
	{
		const int COUNT = 1000;
		const float cubeScale = 0.2f;
		const float localYOffset = 1.5f; // in parent-local units; world offset = cubeScale * 1.5 = 0.3
		const float twistPerLevel = glm::radians(8.0f);

		ModelComponentBuilder builder(bglDevice, registry);
		HierachySystem hs(registry);

		ModelLoadSettings settings{};
		settings.scaleVec = {1.0f, 1.0f, 1.0f};

		entt::entity prev = entt::null;
		for (int i = 0; i < COUNT; i++)
		{
			entt::entity e = registry.create();
			auto &tc = registry.emplace<TransformComponent>(e);
			tc.setScale({cubeScale, cubeScale, cubeScale});
			builder.buildComponent(e, "/models/cube.obj", settings);

			if (i == 0)
			{
				tc.setTranslation({8.0f, 0.0f, 0.0f});
				hierarchyRoot = e;
			}
			else
			{
				hs.CreateHierachy(prev, e);
				auto &hier = registry.get<TransformHierachyComponent>(e);
				hier.localTranslation = {0.0f, localYOffset, 0.0f};
				hier.localRotation = {0.0f, twistPerLevel, 0.0f};
				hier.localScale = {1.0f, 1.0f, 1.0f};
			}
			prev = e;
		}
	}
	void MyApplication::createDirectionalLight()
	{
		const auto entity = registry.create();
		auto &sun = registry.emplace<DirectionalLightComponent>(entity);
		sun.color = {0.6f, 0.6f, 0.6f, 1.0f};
		sun.rotation = {-152.0f, 30.0f, 0.0f};
		sun.shadowBiasMin = 0.0f;
		sun.shadowBiasSlope = 0.0f;
		sun.lux = 2000.0f;
	}
	void MyApplication::createLights()
	{
		std::vector<glm::vec3> lightColors{
			{1.f, .1f, .1f},
			{.1f, .1f, 1.f},
			{.1f, 1.f, .1f},
			{1.f, 1.f, .1f},
			{.1f, 1.f, 1.f},
			{1.f, 1.f, 1.f}};
		for (int i = 0; i < (int)lightColors.size(); i++)
		{
			auto rot = glm::rotate(
				glm::mat4(1.0f),
				(i * glm::two_pi<float>() / lightColors.size()),
				{0.f, -1.f, 0.f});
			const auto entity = registry.create();
			registry.emplace<TransformComponent>(entity, (rot * glm::vec4(glm::vec3{3.f, 1.0f, 0.0f}, 1.0f)));
			registry.emplace<InfoComponent>(entity);
			auto &light = registry.emplace<PointLightComponent>(entity);
			light.color = glm::vec4(lightColors[i], 1.0f);
			light.lux = 800.0f;
		}
	}
	// One textured/skinned entity at origin. Skinned models (glb with JOINTS_0/WEIGHTS_0) get
	// their skin influences + baked palette uploaded and an AnimationComponent attached by the
	// builder, so they render through the AnimatedGBufferRenderSystem automatically.
	void MyApplication::loadModel(const char *path, float scale, ModelLoadSettings settings)
	{
		ModelComponentBuilder builder(bglDevice, registry);
		builder.setTextureLoader(&materialManager->getTextureLoader());
		builder.setMaterialManager(materialManager.get());
		builder.setSkinManager(skinManager.get());

		auto entity = registry.create();
		registry.emplace<TransformComponent>(entity).setScale({scale, scale, scale});
		builder.buildComponent(entity, path, settings);
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
	void MyApplication::loadDragon() { loadModel("/models/chinesedragon.gltf", 0.3f); }
	void MyApplication::loadMonkeyBone() { loadModel("/models/monkey_bone_anim/monkeybone.glb", 1.0f); }
	void MyApplication::loadIKLeg() { loadModel("/models/ikleg/ikbone.glb", 1.0f); }

	// Single LDraw brick at origin, routed to LDrawModelLoader by the ".dat" extension.
	// settings.scale bakes the raw LDU geometry (1 LDU = 0.4 mm) down to engine size at
	// load time. LDraw is -Y up, so a rigid 180° flip about X puts the studs up (a pure
	// rotation keeps winding/normals correct — no negative-scale mirroring).
	void MyApplication::loadLegoBrick()
	{
		// Smoke test: 32316 = "Technic Beam 5" (studless 1x5 stick with pin holes).
		partSystem_->spawnLegoPart("32316", glm::vec3(0.0f));
	}

	// Flat ground for the lego scene: a procedural floor quad (y=0, sized by scaleVec) for the
	// visual, plus a thin STATIC box collider whose TOP face sits at y=0 so it lines up with the
	// quad and gives dropped bricks something to land on.
	void MyApplication::loadLegoGround()
	{
		ModelComponentBuilder builder(bglDevice, registry);
		builder.setTextureLoader(&materialManager->getTextureLoader());
		builder.setMaterialManager(materialManager.get());
		builder.setSkinManager(skinManager.get());

		// generateFloor() lays out the quad using scaleVec.x/.z (see model_loaders/generated.cpp),
		// so the mesh is already full-size; leave the entity transform at identity.
		ModelLoadSettings settings{};
		settings.scaleVec = {100.0f, 1.0f, 100.0f};

		auto entity = registry.create();
		auto &tc = registry.emplace<TransformComponent>(entity); // at origin
		// generateFloor() already sizes the quad via scaleVec, so keep the transform at identity
		// scale (the TransformComponent default is 0.1, which would shrink the visible quad to
		// 1/10 of the collider — the same double-scale trap as the bricks above).
		tc.setScale({1.0f, 1.0f, 1.0f});
		builder.buildComponent(entity, "/models/floor.obj", settings);

		BGLJolt::PhysicsBodyCreationInfo info{};
		info.pos = {0.0f, 0, 0.0f};
		info.rot = {0.0f, 0.0f, 0.0f};
		info.physicsType = PhysicsType::STATIC;
		info.activate = false;
		info.layer = PhysicsLayers::NON_MOVING;
		BGLJolt::GetInstance()->AddBox(entity, {100.0f, 0, 100.0f}, info);

		CONSOLE->Log("Lego", "Spawned ground plane");
	}

} // namespace bagel

int main()
{
	bagel::MyApplication app{};
	try
	{
		app.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
	return 0;
}
