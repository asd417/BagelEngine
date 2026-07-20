#include "my_test_application.hpp"
#include "bagel_hierachy.hpp"
#include "bagel_util.hpp"
#include "imgui/bagel_imgui.hpp" // ImGui + ConsoleApp (CONSOLE)
#include "map/bagel_map_io.hpp"
#include "map/bagel_text_map.hpp"
#include "model/model_component_builder.hpp"
#include "physics/bagel_jolt.hpp"

#include <cmath>
#include <iostream>
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
}
void MyApplication::OnSceneLoad()
{
    buildScene(2); // start on the hierarchy scene
}

void MyApplication::OnUpdate(BGLCamera &camera, float dt)
{
    // Left-click picking: cast a ray from the cursor into the physics scene and select
    // the entity under it (BGLJolt::PickEntity resolves group compounds down to the member).
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
                CONSOLE->Log("Pick", "Selected entity " + std::to_string(static_cast<uint32_t>(entt::to_integral(selectedEntity))));
            else
                CONSOLE->Log("Pick", "Nothing under cursor");
        }
        mouseLeftPrev = mouseLeftDown;
    }
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
    case 6:
        return "sponza_stress";
    case 7:
        return "sponza_instanced";
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

    // Reframe the free-fly camera on every scene switch. The viewer's pose otherwise
    // carries across builds (viewerPosCache/viewerRotCache in Application::run), so after
    // visiting a scene that parks the camera far away — e.g. the x1000 grids at ~140 units
    // out — or after free-flying, the next scene renders off-screen and looks empty. Set a
    // near-origin default here; scenes that need a special vantage (the grids) override it
    // by calling setSpawnCameraPos() from their build function below.
    setSpawnCameraPos({0.0f, -3.0f, -8.0f}); // 3 up, 8 back, looking +Z at the origin
    setSpawnCameraRot({0.0f, 0.0f, 0.0f});   // clear any carried-over free-fly rotation

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
        createLights();
        createDirectionalLight();
        loadSponzaStress();
        break;
    case 7:
        createLights();
        createDirectionalLight();
        loadSponzaInstanced();
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
    // rebuild transient GPU/material/physics state (moved to map/bagel_map_io.*)
    ModelComponentBuilder rebuildBuilder(bglDevice, registry);
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

// Build a human-readable YAML static map at /maps/<name>.yaml into the live scene. Mirrors the
// buildScene() teardown (unload + skin-table reset + camera reframe), then hands the parse to
// TextMap::load, which cooks props through a ModelComponentBuilder. An info_player_start in the
// map overrides the default camera framing. Returns false (and logs) on missing file / parse error.
bool MyApplication::loadTextMap(const std::string &name)
{
    const std::string path = util::enginePath((std::string("/maps/") + name + ".yaml").c_str());
    if (!TextMap::exists(path))
    {
        CONSOLE->Log("TextMap", "No text map file: " + path);
        return false;
    }

    // Drop the current scene (waits for GPU, tears down physics, clears ECS) + reset skin allocator.
    Map::unload(registry);
    materialManager->clearSkinTable();
    hierarchyRoot = entt::null;

    // Default near-origin framing; a map's info_player_start overrides it below (see buildScene).
    setSpawnCameraPos({0.0f, -3.0f, -8.0f});
    setSpawnCameraRot({0.0f, 0.0f, 0.0f});

    ModelComponentBuilder builder(bglDevice, registry);
    TextMap::SpawnPoint spawn;
    if (!TextMap::load(registry, path, builder, *materialManager, *skinManager, spawn))
    {
        CONSOLE->Log("TextMap", "FAILED to load " + path);
        return false;
    }
    if (spawn.set)
    {
        setSpawnCameraPos(spawn.position);
        setSpawnCameraRot(glm::radians(spawn.rotation)); // SpawnPoint rotation is degrees; viewer is radians
    }

    currentMapName = name;
    logDescriptorUsage();
    CONSOLE->Log("TextMap", std::string("Built text map '") + name + "'");
    return true;
}

// "textmap <name>" console command: build /maps/<name>.yaml. Returns a status message.
std::string MyApplication::consoleLoadTextMap(const std::string &name)
{
    if (name.empty())
        return "textmap <name>: build /maps/<name>.yaml";
    if (!loadTextMap(name))
        return "[error] textmap: failed to build " + name;
    return "Built text map '" + name + "'";
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
    if (ImGui::Button("Sponza x1000"))
        buildScene(6);
    ImGui::SameLine();
    if (ImGui::Button("Sponza x1000 (instanced)"))
        buildScene(7);

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

    ImGui::Separator();
    ImGui::TextUnformatted("Text map (YAML):");
    {
        const std::string path = util::enginePath("/maps/textmap.yaml");
        const bool onDisk = TextMap::exists(path);
        std::string label = std::string("Build textmap") + (onDisk ? "" : " (none)");
        if (ImGui::Button(label.c_str()))
            loadTextMap("textmap");
    }

    // NOTE: the live planet LOD/noise controls are removed while the geodesic-CDLOD
    // terrain is mid-refactor (PlanetComponent no longer exposes a live terrain tree).

    ImGui::End();

    DrawRegistryPanel(registry, poseGizmo);
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
    sun.color = {0.6f, 0.6f, 0.94f, 1.0f};
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
            (static_cast<float>(i) * glm::two_pi<float>() / static_cast<float>(lightColors.size())),
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
// Draw-call / culling stress test: SPONZA_STRESS_COUNT copies of Sponza on a flat grid.
// Cheap to build despite the count — ModelCacheManager keys on the source path, so the
// first buildComponent loads the geometry, uploads the buffers and reserves the skin
// block, and every later one is a pointer copy. The whole scene costs one Sponza's worth
// of VRAM, 69 bindless textures and 25 skin entries; what actually scales is the
// per-entity transform and the draw calls.
void MyApplication::loadSponzaStress()
{
    constexpr int SPONZA_STRESS_COUNT = 1000;
    constexpr int SIDE = 32;         // grid columns; rows = COUNT/SIDE
    constexpr float SPACING = 40.0f; // Sponza is ~30 units wide at 0.01 scale
    constexpr float SCALE = 0.01f;

    ModelComponentBuilder builder(bglDevice, registry);
    builder.setTextureLoader(&materialManager->getTextureLoader());
    builder.setMaterialManager(materialManager.get());
    builder.setSkinManager(skinManager.get());

    ModelLoadSettings settings{};
    // Match loadSponza: keep each source mesh as its own submesh so per-submesh frustum
    // culling can skip off-screen chunks. Sponza is 103 primitives, so this is also the
    // difference between ~103k draws and ~1k when the whole grid is on screen.
    settings.mergeSolidSubmeshes = false;

    const float half = (SIDE - 1) * 0.5f;
    for (int i = 0; i < SPONZA_STRESS_COUNT; i++)
    {
        entt::entity e = registry.create();
        auto &tfc = registry.emplace<TransformComponent>(e);
        tfc.setTranslation({static_cast<float>(i % SIDE) * SPACING - half * SPACING,
                            0.0f,
                            static_cast<float>(i / SIDE) * SPACING - half * SPACING});
        tfc.setScale({SCALE, SCALE, SCALE});
        builder.buildComponent(e, "/models/sponza/Sponza.gltf", settings);
    }

    // Y is down. Park the camera off the -Z edge, above the grid, looking down +Z at the
    // whole field — i.e. the worst case, where culling saves nothing.
    setSpawnCameraPos({0.0f, -60.0f, -(half * SPACING + 80.0f)});
}
// Same grid as loadSponzaStress, but every copy lives on ONE entity: the geometry is
// bound once and drawn with an instanced draw whose per-instance model matrices come from
// a single storage buffer (TransformArrayComponent -> DataBufferComponent). This trades
// 1000 entities and 1000 draws for one of each — the instanced path in the gbuffer render
// system (view<TransformArrayComponent, ModelComponent>) uses transform.count() as the
// instance count and reads the matrices via BufferedTransformHandle. No per-submesh frustum
// culling here (the whole batch is one draw), so keep the count at MAX_TRANSFORM_PER_ENT.
void MyApplication::loadSponzaInstanced()
{
    constexpr int SPONZA_INSTANCE_COUNT = MAX_TRANSFORM_PER_ENT; // buffer capacity == array capacity
    constexpr int SIDE = 32;                                     // grid columns; rows = COUNT/SIDE
    constexpr float SPACING = 40.0f;                             // Sponza is ~30 units wide at 0.01 scale
    constexpr float SCALE = 0.01f;

    ModelComponentBuilder builder(bglDevice, registry);
    builder.setTextureLoader(&materialManager->getTextureLoader());
    builder.setMaterialManager(materialManager.get());
    builder.setSkinManager(skinManager.get());

    ModelLoadSettings settings{};
    settings.mergeSolidSubmeshes = false;

    entt::entity e = registry.create();
    builder.buildComponent(e, "/models/sponza/Sponza.gltf", settings);

    // Fill one TransformArrayComponent with the whole grid, then bake it into a GPU
    // storage buffer. setTransform writes an index in place; maxIndex is the instance count.
    auto &tac = registry.emplace<TransformArrayComponent>(e);
    const float half = (SIDE - 1) * 0.5f;
    for (int i = 0; i < SPONZA_INSTANCE_COUNT; i++)
    {
        tac.setTransform(i,
                         {static_cast<float>(i % SIDE) * SPACING - half * SPACING,
                          0.0f,
                          static_cast<float>(i / SIDE) * SPACING - half * SPACING},
                         {SCALE, SCALE, SCALE});
    }
    tac.maxIndex = SPONZA_INSTANCE_COUNT;

    // The DataBufferComponent owns the mapped GPU buffer + bindless handle; it must live on
    // the entity so it outlives this call. ToBufferComponent writes the matrices and flips
    // the component onto the buffered path (usingBuffer = true, bufferHandle set).
    auto &dbc = registry.emplace<DataBufferComponent>(
        e, bglDevice, *descriptorManager,
        static_cast<uint32_t>(sizeof(TransformArrayComponent::TransformBufferUnit)),
        "SponzaInstancedTransforms");
    tac.ToBufferComponent(dbc);

    // Match loadSponzaStress's vantage point.
    setSpawnCameraPos({0.0f, -60.0f, -(half * SPACING + 80.0f)});
}
// Text glTF with a base64-embedded buffer and solid-color (baseColorFactor) materials.
// ~14x6x10 units; scaled down so it sits comfortably in view.
void MyApplication::loadDragon()
{
    loadModel("/models/chinesedragon.gltf", 0.3f);
}
void MyApplication::loadMonkeyBone()
{
    loadModel("/models/monkey_bone_anim/monkeybone.glb", 1.0f);
}
void MyApplication::loadIKLeg()
{
    loadModel("/models/ikleg/ikbone.glb", 1.0f);
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
