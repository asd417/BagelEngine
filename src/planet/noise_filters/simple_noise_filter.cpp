#include "planet/noise_filters/simple_noise_filter.hpp"
namespace bagel
{
float SimpleNoiseFilter::evaluate(const glm::vec3 &point, const NoiseSettings &settings) const
{
    float noiseValue = 0.0f;
    float frequency = settings.baseRoughness;
    float amplitude = 1.0f;
    for (int i = 0; i < settings.numLayers; i++)
    {
        float v = noise.evaluate(point * frequency + settings.center);
        noiseValue += (v + 1) * 0.5f * amplitude;
        frequency *= settings.roughness;
        amplitude *= settings.persistence;
    }
    noiseValue = noiseValue - settings.minValue > 0.0f ? noiseValue - settings.minValue : 0.0f;
    return noiseValue * settings.strength;
}
} // namespace bagel