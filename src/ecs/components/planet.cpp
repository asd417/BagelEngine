#include "planet.hpp"
#include "model/model_component_builder.hpp"
#include "texture/bagel_textures.hpp"
#include "bagel_material.hpp"
#include "transform.hpp"

#include <string>
#include <cassert>
#include <cstring>
#include <array>
#include <vector>
#include <unordered_map>
#include <iostream>

namespace bagel
{
    //Thank you Sebastian Lague
    void PlanetComponentSystem::constructMesh(ModelComponent& mc, PlanetComponent& pc)
    {
        for (int i = 0; i < 6;i++)
        {
            const uint16_t resolution = pc.faces[i].resolution;
            const glm::vec3& localUp = pc.faces[i].localUp;
            const glm::vec3& axisA = pc.faces[i].axisA;
            const glm::vec3& axisB = pc.faces[i].axisB;
            std::vector<glm::vec3> vertices;
            std::vector<int> triangles; //indicies
            vertices.resize(resolution * resolution);
            triangles.resize((resolution - 1) * (resolution - 1) * 6);
            int triIndex = 0;
            for (uint16_t y = 0; y < resolution;y++)
            {
                for (uint16_t x = 0; x < resolution;x++)
                {
                    uint16_t index = x + y * resolution;
                    const glm::vec2 percent = {x / (resolution - 1), y / (resolution - 1)};
                    
                    glm::vec3 pointOnUnitCube = localUp + (percent.x - 0.5f) * 2 * axisA + (percent.y - 0.5f) * 2 * axisB;
                    vertices[index] = pointOnUnitCube;

                    // this engine uses CCW winding order
                    // 0 1 2 3
                    // 4 5 6 7
                    // resolution = 4

                    // tri 1 = {0 5 1} -> {i i+resolution+1 i+1}
                    // tri 2 = {0 4 5} -> {i i+resolution i+resolution+1}
                    if(x != resolution-1 && y != resolution-1)
                    {
                        //tri 1
                        triangles[triIndex] = i;
                        triangles[triIndex+1] = i+resolution+1;
                        triangles[triIndex+2] = i+1;
                        //tri 2
                        triangles[triIndex+3] = i;
                        triangles[triIndex+4] = i+resolution;
                        triangles[triIndex+5] = i+resolution+1;
                        triIndex += 6;
                    }
                }
            }
        }
        ModelComponentBuilder builder(bglDevice, registry);

    }
}