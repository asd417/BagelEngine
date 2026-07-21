#pragma once
#include <glm/vec3.hpp>

#include "math/simplex_noise.hpp"
#include "planet/noise_setting.hpp"

namespace bagel
{
// Fractal Brownian motion layered on top of the engine's gradient (simplex) noise.
// Simplex is a strict improvement over classic Perlin (no directional artifacts,
// cheaper in 3D), so we reuse SimplexNoise rather than ship a second noise field.

class SimpleNoiseFilter
{
  public:
    SimpleNoiseFilter(int seed = 0) : noise(seed)
    {
    }

    // Elevation offset for a point on the unit sphere; >= 0.
    float evaluate(const glm::vec3 &point, const NoiseSettings &settings) const;

  private:
    SimplexNoise noise{};
};
} // namespace bagel
