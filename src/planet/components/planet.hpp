#pragma once
#include <cstdint>
#include <entt.hpp>
#include <vector>

#include "ecs/components/model.hpp"
#include "engine/bagel_engine_device.hpp"
#include "model/bagel_model.hpp"
#include "planet/noise_filters/ridges_noise_filter.hpp"
#include "planet/noise_filters/simple_noise_filter.hpp"

namespace bagel
{
class BGLTextureLoader;   // bagel_textures.hpp (kept out of this header to stay light)
class BGLMaterialManager; // bagel_material.hpp

#define MAX_NOISE_LAYERS 8
#define MAX_PLANET_RESOLUTION 256
#define MAX_GRADIENT_POINTS 8

// One stop of the planet's elevation colour gradient. `height` is a normalized key in [0,1]
// (0 = the mesh's lowest point, 1 = its highest); the surface colour is the gradient sampled at
// that vertex's normalized elevation. Planets are shaded by this gradient, not by texture maps.
struct GradientPoint
{
    glm::vec3 color = {1.0f, 1.0f, 1.0f};
    float height = 0.0f;
};

struct TerrainFace
{
    glm::vec3 localUp;
    glm::vec3 axisA;
    glm::vec3 axisB;

    TerrainFace(glm::vec3 _localUp)
    {
        localUp = _localUp;
        axisA = {localUp.y, localUp.z, localUp.x};
        axisB = glm::cross(localUp, axisA);
    }
};

struct PlanetComponent
{
    float radius = 50.0f;
    uint32_t resolution = 50; // shared by all six faces
    TerrainFace faces[6] = {{{0, 0, 1}}, {{0, 0, -1}}, {{1, 0, 0}}, {{-1, 0, 0}}, {{0, 1, 0}}, {{0, -1, 0}}};
    bool meshBuilt = false;
    // Raised whenever the registry panel edits radius/resolution/noise; the per-frame planet pass
    // in OnUpdate consumes it, regenerates the mesh (editComponent), and clears it. Keeps GPU work
    // out of the imgui panel, which has no BGLDevice access.
    bool dirty = false;
    // NoiseSettings settings = {};
    NoiseLayer layers[MAX_NOISE_LAYERS];
    // Elevation colour gradient (used instead of albedo texture maps). Only the first
    // gradientCount stops are active; keep them sorted by height. The gradient is evaluated
    // per-fragment in the planet shader, fed via push constant — not baked into vertices.
    GradientPoint gradient[MAX_GRADIENT_POINTS] = {
        {{0.05f, 0.15f, 0.40f}, 0.0f}, // low
        {{0.85f, 0.85f, 0.80f}, 1.0f}, // high
    };
    int gradientCount = 2;
    // Elevation span across the built mesh (|pos|/radius - 1). Computed at build time and pushed
    // to the shader so it can normalize each fragment's height into the gradient's [0,1] key.
    float minElevation = 0.0f;
    float maxElevation = 0.0f;
};

class PlanetComponentSystem
{
  public:
    PlanetComponentSystem(BGLDevice &device, entt::registry &r) : bglDevice(device), registry(r) {};
    // Build (or rebuild) a planet mesh from the entity's PlanetComponent (its noise layers, radius,
    // resolution). Pass an existing entity to regenerate it in place — its components are kept
    // (get_or_emplace); pass entt::null (default) to spawn a fresh one with default settings.
    entt::entity constructMesh(entt::entity entity = entt::null);
    BGLModel::Vertex calculateVertexOnPlanet(const NoiseLayer (&layers)[MAX_NOISE_LAYERS], const glm::vec3 &pos, float radius, uint32_t x, uint32_t y, uint32_t resolution);
    // Elevation-displaced position of a unit-sphere point: unit * radius * (1 + sum of enabled layers).
    glm::vec3 displacedPoint(const NoiseLayer (&layers)[MAX_NOISE_LAYERS], const glm::vec3 &unit, float radius);

  private:
    float evaluateNoiseFilter(const glm::vec3 &unit, const NoiseSettings &settings, NoiseType type);
    BGLDevice &bglDevice;
    entt::registry &registry;
    SimpleNoiseFilter simpleNoiseFilter = {};
    RidgesNoiseFilter ridgesNoiseFilter = {};
};
} // namespace bagel