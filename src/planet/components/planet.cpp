#include "planet.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "bagel_material.hpp"
#include "ecs/components/transform.hpp"
#include "engine/bagel_descriptors.hpp"
#include "model/bagel_model.hpp"
#include "model/model_component_builder.hpp"
#include "planet/noise_setting.hpp"
#include "texture/bagel_textures.hpp"

namespace bagel
{
float PlanetComponentSystem::evaluateNoiseFilter(const glm::vec3 &unit, const NoiseSettings &settings, NoiseType type)
{
    switch (type)
    {
    case (SIMPLE):
        return simpleNoiseFilter.evaluate(unit, settings);
    case (RIDGES):
        return ridgesNoiseFilter.evaluate(unit, settings);
    default:
        return 0.0f;
    }
}
glm::vec3 PlanetComponentSystem::displacedPoint(const NoiseLayer (&layers)[MAX_NOISE_LAYERS], const glm::vec3 &unit, float radius)
{

    float firstLayerValue = (layers[0].enabled) ? evaluateNoiseFilter(unit, layers[0].settings, layers[0].type) : 0.0f;
    float elevation = firstLayerValue;
    for (int i = 1; i < MAX_NOISE_LAYERS; i++)
    {
        if (layers[i].enabled)
        {
            float mask = (layers[i].useFirstLayerAsMask) ? firstLayerValue : 1.0f;
            elevation += evaluateNoiseFilter(unit, layers[i].settings, layers[i].type) * mask;
        }
    }
    return unit * radius * (1.0f + elevation);
}

BGLModel::Vertex PlanetComponentSystem::calculateVertexOnPlanet(const NoiseLayer (&layers)[MAX_NOISE_LAYERS], const glm::vec3 &pos, float radius, uint32_t x, uint32_t y, uint32_t resolution)
{
    const glm::vec3 center = displacedPoint(layers, pos, radius);

    // Analytic normal: perturb the point along two sphere-tangent directions, re-displace, and
    // cross the resulting surface deltas. This samples the displacement field directly, so it is
    // seamless across the 6 cube faces — unlike averaging per-face triangle normals, which creases
    // at the shared edges (each face owns its own duplicated edge vertices).
    const glm::vec3 up = (pos.y * pos.y < 0.9801f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0); // not parallel to pos
    const glm::vec3 tangent = glm::normalize(glm::cross(up, pos));
    const glm::vec3 bitangent = glm::cross(pos, tangent);

    const float eps = 0.001f;
    const glm::vec3 dA = displacedPoint(layers, glm::normalize(pos + tangent * eps), radius) - center;
    const glm::vec3 dB = displacedPoint(layers, glm::normalize(pos + bitangent * eps), radius) - center;
    glm::vec3 normal = glm::normalize(glm::cross(dA, dB));
    if (glm::dot(normal, pos) < 0.0f) // keep it pointing outward
        normal = -normal;

    return {
        center,
        {0, 0, 0},
        normal,
        {},
        {static_cast<float>(x) / static_cast<float>(resolution - 1), static_cast<float>(y) / static_cast<float>(resolution - 1)},
        0};
}

// Thank you Sebastian Lague
entt::entity PlanetComponentSystem::constructMesh(entt::entity entity)
{
    // Reuse a caller-supplied entity (rebuild path); spawn a fresh one only when none was given.
    if (!registry.valid(entity))
        entity = registry.create();

    // get_or_emplace keeps any components a reused entity already carries (edited noise layers,
    // transform, the built mesh) and creates them fresh on a new entity.
    (void)registry.get_or_emplace<TransformComponent>(entity);
    auto &mc = registry.get_or_emplace<ModelComponent>(entity);
    auto &pc = registry.get_or_emplace<PlanetComponent>(entity);

    const uint32_t resolution = pc.resolution;

    std::vector<BGLModel::Vertex> vertices;
    std::vector<uint32_t> triangles; // indices
    std::vector<SubmeshInfo> submeshes;
    vertices.resize(resolution * resolution * 6);
    triangles.resize((resolution - 1) * (resolution - 1) * 6 * 6);
    submeshes.resize(6);
    for (int i = 0; i < 6; i++)
    {
        const glm::vec3 &localUp = pc.faces[i].localUp;
        const glm::vec3 &axisA = pc.faces[i].axisA;
        const glm::vec3 &axisB = pc.faces[i].axisB;

        uint32_t submeshVertStart = resolution * resolution * i;
        uint32_t submeshIndexStart = (resolution - 1) * (resolution - 1) * 6 * i;
        submeshes[i].firstIndex = submeshIndexStart;
        submeshes[i].firstVertex = submeshVertStart;
        submeshes[i].vertexCount = resolution * resolution;
        submeshes[i].indexCount = (resolution - 1) * (resolution - 1) * 6;

        uint32_t triIndex = submeshIndexStart;
        for (uint32_t y = 0; y < resolution; y++)
        {
            for (uint32_t x = 0; x < resolution; x++)
            {
                uint32_t vertIndex = x + y * resolution + submeshVertStart;
                const glm::vec2 percent = {static_cast<float>(x) / static_cast<float>(resolution - 1), static_cast<float>(y) / static_cast<float>(resolution - 1)};

                glm::vec3 pointOnUnitCube = localUp + (percent.x - 0.5f) * 2 * axisA + (percent.y - 0.5f) * 2 * axisB;
                glm::vec3 pointOnUnitSphere = glm::normalize(pointOnUnitCube);
                vertices[vertIndex] = calculateVertexOnPlanet(pc.layers, pointOnUnitSphere, pc.radius, x, y, resolution);

                // this engine uses CCW winding order
                // 0 1 2 3
                // 4 5 6 7
                // resolution = 4

                // CCW winding viewed from OUTSIDE (localUp side): cross(edge1,edge2) must point
                // along +localUp. axisB = cross(localUp, axisA), so cross(axisA, axisB) = +localUp;
                // the triangles are ordered so their first two edges reproduce that.
                // tri 1 = {0 1 5} -> {i i+1 i+resolution+1}
                // tri 2 = {0 5 4} -> {i i+resolution+1 i+resolution}
                if (x != resolution - 1 && y != resolution - 1)
                {
                    // tri 1
                    triangles[triIndex] = vertIndex;
                    triangles[triIndex + 1] = vertIndex + 1;
                    triangles[triIndex + 2] = vertIndex + resolution + 1;
                    // tri 2
                    triangles[triIndex + 3] = vertIndex;
                    triangles[triIndex + 4] = vertIndex + resolution + 1;
                    triangles[triIndex + 5] = vertIndex + resolution;
                    triIndex += 6;
                }
            }
        }
    }
    // Record the mesh's elevation span (|pos|/radius - 1) so the planet shader can normalize each
    // fragment's height into the gradient's [0,1] key. The gradient itself is evaluated on the GPU.
    {
        float minH = std::numeric_limits<float>::max();
        float maxH = std::numeric_limits<float>::lowest();
        for (const auto &v : vertices)
        {
            const float h = glm::length(v.position) / pc.radius - 1.0f;
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
        pc.minElevation = minH;
        pc.maxElevation = maxH;
    }

    ModelComponentBuilder builder(bglDevice, registry);
    if (!pc.meshBuilt)
    {
        // First build: allocate mapped (CPU-writable) buffers so later rebuilds can edit in place.
        builder.buildComponent(mc, "planet", vertices, triangles, submeshes, true);
        pc.meshBuilt = true;
    }
    else
    {
        builder.editComponent(mc, "planet", vertices, triangles, submeshes);
    }
    return entity;
} // namespace bagel
} // namespace bagel