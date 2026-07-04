#pragma once
// -----------------------------------------------------------------------------
// Cube <-> sphere addressing — the CPU half of a CPU/GPU shared mapping.
//
// MUST stay bit-compatible with shaders/cubemap.glsl::dirToFaceUV (the same way
// noise.glsl mirrors bagel_math.hpp::perlin). Standard GL cube convention: the
// major axis selects one of six faces; the other two components / |major| give
// uv in [-1,1] -> [0,1]. Used by the planet's cubesphere geometry (and any other
// per-face cube addressing). Header-only (glm + std, like bagel_math.hpp).
// -----------------------------------------------------------------------------
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bagel::planet
{
    // Face order: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z.
    inline constexpr int CUBE_FACES = 6;

    // Unit direction -> (face, uv in [0,1]). GL cube convention; see file header.
    inline void dirToFaceUV(const glm::vec3 &d, int &face, glm::vec2 &uv)
    {
        float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
        float ma, sc, tc;
        if (ax >= ay && ax >= az) {
            if (d.x >= 0.0f) { face = 0; sc = -d.z; tc = -d.y; }
            else             { face = 1; sc =  d.z; tc = -d.y; }
            ma = ax;
        } else if (ay >= az) {
            if (d.y >= 0.0f) { face = 2; sc =  d.x; tc =  d.z; }
            else             { face = 3; sc =  d.x; tc = -d.z; }
            ma = ay;
        } else {
            if (d.z >= 0.0f) { face = 4; sc =  d.x; tc = -d.y; }
            else             { face = 5; sc = -d.x; tc = -d.y; }
            ma = az;
        }
        uv.x = 0.5f * (sc / ma + 1.0f);
        uv.y = 0.5f * (tc / ma + 1.0f);
    }

    // Inverse of dirToFaceUV: (face, uv) -> unit direction.
    inline glm::vec3 faceUVToDir(int face, const glm::vec2 &uv)
    {
        float sc = uv.x * 2.0f - 1.0f, tc = uv.y * 2.0f - 1.0f;
        glm::vec3 d;
        switch (face) {
        case 0:  d = { 1.0f, -tc, -sc }; break; // +X
        case 1:  d = { -1.0f, -tc, sc }; break; // -X
        case 2:  d = { sc, 1.0f, tc };   break; // +Y
        case 3:  d = { sc, -1.0f, -tc }; break; // -Y
        case 4:  d = { sc, -tc, 1.0f };  break; // +Z
        default: d = { -sc, -tc, -1.0f }; break; // -Z
        }
        return glm::normalize(d);
    }

} // namespace bagel::planet
