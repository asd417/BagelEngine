#include "planet/noise_filters/ridges_noise_filter.hpp"
#include <algorithm>
#include <math.h>
namespace bagel
{
float RidgesNoiseFilter::evaluate(const glm::vec3 &point, const NoiseSettings &settings) const
{
    float noiseValue = 0.0f;
    float frequency = settings.baseRoughness;
    float amplitude = 1.0f;
    float weight = 1.0f;
    for (int i = 0; i < settings.numLayers; i++)
    {
        float v = 1 - std::abs(noise.evaluate(point * frequency + settings.center));
        v *= v;
        v *= weight;
        weight = std::clamp(v, 0.0f, 1.0f);
        noiseValue += v * amplitude;
        frequency *= settings.roughness;
        amplitude *= settings.persistence;
    }
    noiseValue = noiseValue - settings.minValue > 0.0f ? noiseValue - settings.minValue : 0.0f;
    return noiseValue * settings.strength;
}
} // namespace bagel