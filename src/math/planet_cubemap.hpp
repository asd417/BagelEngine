#pragma once
// -----------------------------------------------------------------------------
// Planet paint cube-map addressing — the CPU half of a CPU/GPU shared mapping.
//
// MUST stay bit-compatible with shaders/cubemap.glsl::dirToFaceUV (same way
// noise.glsl mirrors bagel_math.hpp::perlin), or the painted geometry (sampled
// here for vertex displacement) drifts from the painted shading (sampled there
// per-fragment). Standard GL cube convention: the major axis selects one of six
// faces; the other two components / |major| give uv in [-1,1] -> [0,1].
//
// The painted height delta is stored as R16_UNORM: 0.5 (== 32768) is zero delta,
// decoded as ((u16/65535)*2 - 1) * heightScale (world units). Header-only (glm +
// std, like bagel_math.hpp) so it compiles standalone.
// -----------------------------------------------------------------------------
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bagel::planet
{
    // Face order: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z. The 6 paint faces are stored
    // back-to-back in one flat buffer; face f, column i, row j lives at this index.
    inline constexpr int PAINT_FACES = 6;
    inline uint32_t texelIndex(int face, int i, int j, int res)
    {
        return static_cast<uint32_t>((face * res + j) * res + i);
    }

    inline constexpr uint16_t PAINT_ZERO = 32768; // R16 value for zero delta

    inline float decodePaint(uint16_t u, float heightScale)
    {
        return ((static_cast<float>(u) / 65535.0f) * 2.0f - 1.0f) * heightScale;
    }
    inline uint16_t encodePaint(float delta, float heightScale)
    {
        float n = (heightScale > 0.0f) ? (delta / heightScale) : 0.0f; // -> [-1,1]
        float u = (n * 0.5f + 0.5f) * 65535.0f;
        return static_cast<uint16_t>(std::lround(std::clamp(u, 0.0f, 65535.0f)));
    }

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

    // Inverse of dirToFaceUV: (face, uv) -> unit direction. Used CPU-side to walk
    // a face's texels back to sphere directions when splatting a brush.
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

    // Bilinear paint-delta sample at a direction (matches GPU linear filtering +
    // CLAMP_TO_EDGE). `paint` is the flat 6*res*res R16 buffer.
    inline float samplePaint(const uint16_t *paint, int res, float heightScale, const glm::vec3 &dir)
    {
        int face;
        glm::vec2 uv;
        dirToFaceUV(dir, face, uv);
        float fx = uv.x * res - 0.5f, fy = uv.y * res - 0.5f;
        int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        float tx = fx - x0, ty = fy - y0;
        auto at = [&](int x, int y) {
            x = std::clamp(x, 0, res - 1);
            y = std::clamp(y, 0, res - 1);
            return decodePaint(paint[texelIndex(face, x, y, res)], heightScale);
        };
        float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d2 = at(x0 + 1, y0 + 1);
        return glm::mix(glm::mix(a, b, tx), glm::mix(c, d2, tx), ty);
    }

} // namespace bagel::planet
