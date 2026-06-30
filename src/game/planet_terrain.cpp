#include "planet_terrain.hpp"
#include "math/bagel_math.hpp"

namespace bagel::planet
{
    glm::vec3 PlanetTerrain::surfacePoint(const glm::vec3 &dir) const
    {
        glm::vec3 d = glm::normalize(dir);
        return d * heightAt(d);
    }
}