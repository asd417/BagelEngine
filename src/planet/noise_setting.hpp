#pragma once
#include <glm/vec3.hpp>

namespace bagel
{
enum NoiseType
{
    SIMPLE,
    RIDGES
};
struct NoiseSettings
{
    int numLayers = 8;
    float persistence = 0.5f;
    float baseRoughness = 0.8f;
    float strength = 0.3f;   // overall elevation scale
    float roughness = 1.84f; // lacunarity: frequency multiplier per octave
    glm::vec3 center = {0, 0, 0};
    float minValue = 0.96f;
};
struct NoiseLayer
{
    NoiseSettings settings;
    bool useFirstLayerAsMask = true;
    bool enabled = false;
    NoiseType type = SIMPLE;
};
} // namespace bagel