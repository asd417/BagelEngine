#include "generated.hpp"
// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace bagel
{
    GeneratedModelLoader::GeneratedModelLoader(BGLTextureLoader* pTL) : ModelLoaderBase(pTL)
	{};
    void GeneratedModelLoader::load(ModelLoadSettings parms)
    {
        if (parms.source == "grid")
            generateGrid(parms);
        if (parms.source == "cube")
            generateCube(parms);
        if (parms.source == "floor")
            generateFloor(parms);
        if (parms.source == "sphere")
            generateSphere(parms);
        if (parms.source == "icosphere")
            generateIcosphere(parms);
        if (parms.source == "wirecube")
            generateWireCube(parms);
        if (parms.source == "wiresphere")
            generateWireSphere(parms);
        if (parms.source == "axis")
            generateAxis(parms);
    }
    void GeneratedModelLoader::generateGrid(ModelLoadSettings buildSettings)
    {
        SubmeshInfo gridMesh{};
        int size = static_cast<int>(buildSettings.scale);
        for (int i = 0; i <= size; i++)
        {
            BGLModel::Vertex vertex1{};
            vertex1.position = {i - size / 2, 0, -size / 2};
            vertices.push_back(vertex1);
            BGLModel::Vertex vertex2{};
            vertex2.position = {i - size / 2, 0, size / 2};
            vertices.push_back(vertex2);

            BGLModel::Vertex vertex4{};
            vertex4.position = {-size / 2, 0, i - size / 2};
            vertices.push_back(vertex4);
            BGLModel::Vertex vertex5{};
            vertex5.position = {size / 2, 0, i - size / 2};
            vertices.push_back(vertex5);
        }
        gridMesh.firstIndex = 0;
        gridMesh.indexCount = static_cast<uint32_t>(indices.size());
        gridMesh.firstVertex = 0;
        gridMesh.vertexCount = static_cast<uint32_t>(vertices.size());
        submeshes.push_back(gridMesh);
    }

    void GeneratedModelLoader::generateCube(ModelLoadSettings buildSettings)
    {
        struct Face
        {
            glm::vec3 normal;
            glm::vec3 c[4];
            glm::vec2 uv[4];
        };
        float sx = buildSettings.scaleVec.x * 0.5f;
        float sy = buildSettings.scaleVec.y * 0.5f;
        float sz = buildSettings.scaleVec.z * 0.5f;
        Face faces[] = {
            {{0, 1, 0}, {{-sx, sy, -sz}, {sx, sy, -sz}, {sx, sy, sz}, {-sx, sy, sz}}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
            {{0, -1, 0}, {{-sx, -sy, sz}, {sx, -sy, sz}, {sx, -sy, -sz}, {-sx, -sy, -sz}}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
            {{1, 0, 0}, {{sx, -sy, sz}, {sx, sy, sz}, {sx, sy, -sz}, {sx, -sy, -sz}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}},
            {{-1, 0, 0}, {{-sx, -sy, -sz}, {-sx, sy, -sz}, {-sx, sy, sz}, {-sx, -sy, sz}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}},
            {{0, 0, 1}, {{-sx, -sy, sz}, {-sx, sy, sz}, {sx, sy, sz}, {sx, -sy, sz}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}},
            {{0, 0, -1}, {{sx, -sy, -sz}, {sx, sy, -sz}, {-sx, sy, -sz}, {-sx, -sy, -sz}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}},
        };
        SubmeshInfo sm{};
        sm.firstIndex = 0;
        for (auto &f : faces)
        {
            uint32_t base = static_cast<uint32_t>(vertices.size());
            for (int i = 0; i < 4; i++)
            {
                BGLModel::Vertex v{};
                v.position = f.c[i];
                v.normal = f.normal;
                v.color = {1, 1, 1};
                v.uv = f.uv[i];
                vertices.push_back(v);
            }
            indices.push_back(base);
            indices.push_back(base + 2);
            indices.push_back(base + 1);
            indices.push_back(base);
            indices.push_back(base + 3);
            indices.push_back(base + 2);
        }
        sm.indexCount = static_cast<uint32_t>(indices.size());
        sm.firstVertex = 0;
        sm.vertexCount = static_cast<uint32_t>(vertices.size());
        submeshes.push_back(sm);
    }

    void GeneratedModelLoader::generateFloor(ModelLoadSettings buildSettings)
    {
        float sx = buildSettings.scaleVec.x * 0.5f;
        float sz = buildSettings.scaleVec.z * 0.5f;
        glm::vec3 n{0, 1, 0};
        glm::vec3 pos[] = {{-sx, 0, -sz}, {sx, 0, -sz}, {sx, 0, sz}, {-sx, 0, sz}};
        glm::vec2 uv[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        uint32_t base = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < 4; i++)
        {
            BGLModel::Vertex v{};
            v.position = pos[i];
            v.normal = n;
            v.color = {1, 1, 1};
            v.uv = uv[i];
            vertices.push_back(v);
        }
        // Winding matches generateCube()'s +y top face (0,2,1)(0,3,2) so the front face
        // agrees with the up normal; the reverse order rendered the quad facing down.
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
        SubmeshInfo sm{};
        sm.firstIndex = 0;
        sm.indexCount = static_cast<uint32_t>(indices.size());
        sm.firstVertex = 0;
        sm.vertexCount = static_cast<uint32_t>(vertices.size());
        submeshes.push_back(sm);
    }

    void GeneratedModelLoader::generateSphere(ModelLoadSettings buildSettings)
    {
        const int stacks = 16;
        const int slices = 32;
        float rx = buildSettings.scaleVec.x * 0.5f;
        float ry = buildSettings.scaleVec.y * 0.5f;
        float rz = buildSettings.scaleVec.z * 0.5f;

        uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i <= stacks; i++)
        {
            float theta = glm::pi<float>() * i / stacks;
            float sinT = sinf(theta);
            float cosT = cosf(theta);
            for (int j = 0; j <= slices; j++)
            {
                float phi = glm::two_pi<float>() * j / slices;
                float nx = sinT * cosf(phi);
                float ny = cosT;
                float nz = sinT * sinf(phi);
                BGLModel::Vertex v{};
                v.position = {rx * nx, ry * ny, rz * nz};
                v.normal = {nx, ny, nz};
                v.color = {1, 1, 1};
                v.uv = {(float)j / slices, (float)i / stacks};
                vertices.push_back(v);
            }
        }

        uint32_t baseIndex = static_cast<uint32_t>(indices.size());
        for (int i = 0; i < stacks; i++)
        {
            for (int j = 0; j < slices; j++)
            {
                uint32_t v0 = baseVertex + i * (slices + 1) + j;
                uint32_t v1 = v0 + 1;
                uint32_t v2 = baseVertex + (i + 1) * (slices + 1) + j;
                uint32_t v3 = v2 + 1;
                indices.push_back(v0);
                indices.push_back(v2);
                indices.push_back(v1);
                indices.push_back(v1);
                indices.push_back(v2);
                indices.push_back(v3);
            }
        }

        SubmeshInfo sm{};
        sm.firstVertex = baseVertex;
        sm.vertexCount = static_cast<uint32_t>(vertices.size()) - baseVertex;
        sm.firstIndex = baseIndex;
        sm.indexCount = static_cast<uint32_t>(indices.size()) - baseIndex;
        submeshes.push_back(sm);
    }

    void GeneratedModelLoader::generateIcosphere(ModelLoadSettings buildSettings)
    {
        float rx = buildSettings.scaleVec.x * 0.5f;
        float ry = buildSettings.scaleVec.y * 0.5f;
        float rz = buildSettings.scaleVec.z * 0.5f;
        const int level = 3;

        const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
        std::vector<glm::vec3> sv = {
            glm::normalize(glm::vec3(-1, t, 0)),
            glm::normalize(glm::vec3(1, t, 0)),
            glm::normalize(glm::vec3(-1, -t, 0)),
            glm::normalize(glm::vec3(1, -t, 0)),
            glm::normalize(glm::vec3(0, -1, t)),
            glm::normalize(glm::vec3(0, 1, t)),
            glm::normalize(glm::vec3(0, -1, -t)),
            glm::normalize(glm::vec3(0, 1, -t)),
            glm::normalize(glm::vec3(t, 0, -1)),
            glm::normalize(glm::vec3(t, 0, 1)),
            glm::normalize(glm::vec3(-t, 0, -1)),
            glm::normalize(glm::vec3(-t, 0, 1)),
        };
        std::vector<std::array<uint32_t, 3>> faces = {
            {0, 11, 5},
            {0, 5, 1},
            {0, 1, 7},
            {0, 7, 10},
            {0, 10, 11},
            {1, 5, 9},
            {5, 11, 4},
            {11, 10, 2},
            {10, 7, 6},
            {7, 1, 8},
            {3, 9, 4},
            {3, 4, 2},
            {3, 2, 6},
            {3, 6, 8},
            {3, 8, 9},
            {4, 9, 5},
            {2, 4, 11},
            {6, 2, 10},
            {8, 6, 7},
            {9, 8, 1},
        };

        auto getMidpoint = [&](std::unordered_map<uint64_t, uint32_t> &cache, uint32_t a, uint32_t b) -> uint32_t
        {
            uint64_t key = ((uint64_t)std::min(a, b) << 32) | std::max(a, b);
            auto it = cache.find(key);
            if (it != cache.end())
                return it->second;
            uint32_t idx = static_cast<uint32_t>(sv.size());
            sv.push_back(glm::normalize((sv[a] + sv[b]) * 0.5f));
            cache[key] = idx;
            return idx;
        };

        for (int l = 0; l < level; l++)
        {
            std::unordered_map<uint64_t, uint32_t> cache;
            std::vector<std::array<uint32_t, 3>> next;
            next.reserve(faces.size() * 4);
            for (auto &f : faces)
            {
                uint32_t a = getMidpoint(cache, f[0], f[1]);
                uint32_t b = getMidpoint(cache, f[1], f[2]);
                uint32_t c = getMidpoint(cache, f[2], f[0]);
                next.push_back({f[0], a, c});
                next.push_back({f[1], b, a});
                next.push_back({f[2], c, b});
                next.push_back({a, b, c});
            }
            faces = std::move(next);
        }

        uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
        for (auto &s : sv)
        {
            BGLModel::Vertex v{};
            v.position = {rx * s.x, ry * s.y, rz * s.z};
            v.normal = s;
            v.color = {1, 1, 1};
            v.uv = {
                0.5f + atan2f(s.z, s.x) / glm::two_pi<float>(),
                0.5f - asinf(s.y) / glm::pi<float>()};
            vertices.push_back(v);
        }

        uint32_t baseIndex = static_cast<uint32_t>(indices.size());
        for (auto &f : faces)
        {
            indices.push_back(baseVertex + f[0]);
            indices.push_back(baseVertex + f[1]);
            indices.push_back(baseVertex + f[2]);
        }

        SubmeshInfo sm{};
        sm.firstVertex = baseVertex;
        sm.vertexCount = static_cast<uint32_t>(sv.size());
        sm.firstIndex = baseIndex;
        sm.indexCount = static_cast<uint32_t>(faces.size() * 3);
        submeshes.push_back(sm);
    }

    void GeneratedModelLoader::generateWireCube(ModelLoadSettings buildSettings)
    {
        float sx = buildSettings.scaleVec.x * 0.5f;
        float sy = buildSettings.scaleVec.y * 0.5f;
        float sz = buildSettings.scaleVec.z * 0.5f;
        glm::vec3 c[8] = {
            {-sx, -sy, -sz},
            {sx, -sy, -sz},
            {sx, sy, -sz},
            {-sx, sy, -sz},
            {-sx, -sy, sz},
            {sx, -sy, sz},
            {sx, sy, sz},
            {-sx, sy, sz},
        };
        int edges[12][2] = {
            {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7},
        };
        for (auto &e : edges)
        {
            BGLModel::Vertex v0{}, v1{};
            v0.position = c[e[0]];
            v0.color = {1, 1, 1};
            v1.position = c[e[1]];
            v1.color = {1, 1, 1};
            vertices.push_back(v0);
            vertices.push_back(v1);
        }
        SubmeshInfo sm{};
        sm.firstIndex = 0;
        sm.indexCount = 0;
        sm.firstVertex = 0;
        sm.vertexCount = static_cast<uint32_t>(vertices.size());
        submeshes.push_back(sm);
    }

    void GeneratedModelLoader::generateWireSphere(ModelLoadSettings buildSettings)
    {
        const int N = 32;
        const float rx = buildSettings.scaleVec.x * 0.5f;
        const float ry = buildSettings.scaleVec.y * 0.5f;
        const float rz = buildSettings.scaleVec.z * 0.5f;
        for (int circle = 0; circle < 3; circle++)
        {
            for (int i = 0; i < N; i++)
            {
                float a0 = glm::two_pi<float>() * i / N;
                float a1 = glm::two_pi<float>() * (i + 1) / N;
                glm::vec3 p0, p1;
                if (circle == 0) // XZ plane
                {
                    p0 = {rx * cosf(a0), 0, rz * sinf(a0)};
                    p1 = {rx * cosf(a1), 0, rz * sinf(a1)};
                }
                else if (circle == 1) // XY plane
                {
                    p0 = {rx * cosf(a0), ry * sinf(a0), 0};
                    p1 = {rx * cosf(a1), ry * sinf(a1), 0};
                }
                else // YZ plane
                {
                    p0 = {0, ry * cosf(a0), rz * sinf(a0)};
                    p1 = {0, ry * cosf(a1), rz * sinf(a1)};
                }
                BGLModel::Vertex v0{}, v1{};
                v0.position = p0;
                v0.color = {1, 1, 1};
                v1.position = p1;
                v1.color = {1, 1, 1};
                vertices.push_back(v0);
                vertices.push_back(v1);
            }
        }
        SubmeshInfo sm{};
        sm.firstIndex = 0;
        sm.indexCount = 0;
        sm.firstVertex = 0;
        sm.vertexCount = static_cast<uint32_t>(vertices.size());
        submeshes.push_back(sm);
    }

    void GeneratedModelLoader::generateAxis(ModelLoadSettings buildSettings)
    {
        const int N = 12;
        const float length = buildSettings.scale * 0.5f;
        const float radius = buildSettings.scale * 0.04f;

        struct Arm
        {
            glm::vec3 dir;
            glm::vec3 color;
        };
        Arm arms[] = {{{1, 0, 0}, {1, 0, 0}}, {{0, 1, 0}, {0, 1, 0}}, {{0, 0, 1}, {0, 0, 1}}};

        for (auto &arm : arms)
        {
            glm::vec3 dir = arm.dir;
            glm::vec3 arb = (glm::abs(dir.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::vec3 right = glm::normalize(glm::cross(dir, arb));
            glm::vec3 up = glm::normalize(glm::cross(right, dir));

            uint32_t barrelBase = static_cast<uint32_t>(vertices.size());
            for (int i = 0; i < N; i++)
            {
                float angle = glm::two_pi<float>() * i / N;
                glm::vec3 rad = right * cosf(angle) + up * sinf(angle);
                BGLModel::Vertex bv{}, tv{};
                bv.position = rad * radius;
                bv.normal = rad;
                bv.color = arm.color;
                bv.uv = {(float)i / N, 0};
                tv.position = rad * radius + dir * length;
                tv.normal = rad;
                tv.color = arm.color;
                tv.uv = {(float)i / N, 1};
                vertices.push_back(bv);
                vertices.push_back(tv);
            }
            for (int i = 0; i < N; i++)
            {
                uint32_t i0 = barrelBase + i * 2, i1 = barrelBase + i * 2 + 1;
                uint32_t i2 = barrelBase + (i + 1) % N * 2 + 1, i3 = barrelBase + (i + 1) % N * 2;
                indices.push_back(i0);
                indices.push_back(i1);
                indices.push_back(i2);
                indices.push_back(i0);
                indices.push_back(i2);
                indices.push_back(i3);
            }

            // Top cap
            uint32_t capCenter = static_cast<uint32_t>(vertices.size());
            BGLModel::Vertex tip{};
            tip.position = dir * length;
            tip.normal = dir;
            tip.color = arm.color;
            vertices.push_back(tip);
            for (int i = 0; i < N; i++)
            {
                float angle = glm::two_pi<float>() * i / N;
                glm::vec3 rad = right * cosf(angle) + up * sinf(angle);
                BGLModel::Vertex v{};
                v.position = rad * radius + dir * length;
                v.normal = dir;
                v.color = arm.color;
                vertices.push_back(v);
            }
            for (int i = 0; i < N; i++)
            {
                indices.push_back(capCenter);
                indices.push_back(capCenter + 1 + (i + 1) % N);
                indices.push_back(capCenter + 1 + i);
            }
        }

        SubmeshInfo sm{};
        sm.firstIndex = 0;
        sm.indexCount = static_cast<uint32_t>(indices.size());
        sm.firstVertex = 0;
        sm.vertexCount = static_cast<uint32_t>(vertices.size());
        submeshes.push_back(sm);
    }
}