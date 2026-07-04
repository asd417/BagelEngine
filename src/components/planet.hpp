#pragma once
#include <entt.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include "components/model.hpp"
#include "bagel_engine_device.hpp"

namespace bagel
{
    class BGLTextureLoader;   // bagel_textures.hpp (kept out of this header to stay light)
    class BGLMaterialManager; // bagel_material.hpp

    struct TerrainConfig
    {
        float radius = 100.0f;    // base (sea-level) radius; per-vertex radius starts here
        int minLevel = 0;         // coarsest the cut ever goes — every leaf is at least this deep
        int maxLevel = 5;         // deepest subdivision (20*4^8 ~ 1.3M tris if fully dense)
        float splitFactor = 5.0f; // node splits while camera distance < splitFactor * nodeEdgeLen
        // --- procedural height (fBm over perlin, sampled from vertex dir) ---
        float noiseAmplitude = 8.0f;  // peak height added above/below sea-level radius
        float noiseFrequency = 2.0f;  // base feature frequency (cells per unit sphere)
        int noiseOctaves = 5;         // fBm octaves; more = finer detail
        float noiseLacunarity = 2.0f; // frequency multiplier per octave
        float noiseGain = 0.5f;       // amplitude multiplier per octave
        float sealevel = 1000.0f;     // surface radius cap -> terrain above is flattened to flat seas (high = no cap)
        uint32_t seed = 1337;         // per-planet seed
    };

    struct TerrainFace
    {
        glm::vec3 localUp;
        glm::vec3 axisA;
        glm::vec3 axisB;
        int resolution = 3;
    };

    struct PlanetComponent
    {
        TerrainConfig cfg;
        glm::vec3 planetLastRebuildCam = glm::vec3(1e9f);
        TerrainFace faces[6] = {};
    };

    class PlanetComponentSystem
    {
        public:
            PlanetComponentSystem(BGLDevice& device, entt::registry& r) : bglDevice(device), registry(r) {};
            void constructMesh(ModelComponent &mc, PlanetComponent &pc);
        private:
        BGLDevice& bglDevice;
        entt::registry& registry;
    };
}