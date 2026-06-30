#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace bagel
{
    // 3D simplex noise. Ported from a C# implementation (Evaluate returns ~[-1, 1]).
    // Header-only, glm-based, like bagel_math.hpp. Seed reshuffles the permutation
    // table to produce an independent field; seed 0 uses the canonical table.
    class SimplexNoise
    {
    public:
        SimplexNoise() { randomize(0); }
        explicit SimplexNoise(int seed) { randomize(seed); }

        // Generates a value, typically in range [-1, 1].
        float evaluate(const glm::vec3 &point) const
        {
            double x = point.x;
            double y = point.y;
            double z = point.z;
            double n0 = 0, n1 = 0, n2 = 0, n3 = 0;

            // Skew the input space to determine which simplex cell we're in.
            double s = (x + y + z) * F3;
            int i = fastFloor(x + s);
            int j = fastFloor(y + s);
            int k = fastFloor(z + s);

            double t = (i + j + k) * G3;

            // The x,y,z distances from the cell origin.
            double x0 = x - (i - t);
            double y0 = y - (j - t);
            double z0 = z - (k - t);

            // For the 3D case, the simplex shape is a slightly irregular tetrahedron.
            // Determine which simplex we are in.
            int i1, j1, k1; // offsets for second corner of simplex in (i,j,k) coords
            int i2, j2, k2; // offsets for third corner of simplex in (i,j,k) coords

            if (x0 >= y0)
            {
                if (y0 >= z0)
                {   // X Y Z order
                    i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
                }
                else if (x0 >= z0)
                {   // X Z Y order
                    i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1;
                }
                else
                {   // Z X Y order
                    i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1;
                }
            }
            else // x0 < y0
            {
                if (y0 < z0)
                {   // Z Y X order
                    i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1;
                }
                else if (x0 < z0)
                {   // Y Z X order
                    i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1;
                }
                else
                {   // Y X Z order
                    i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
                }
            }

            // Offsets for second corner in (x,y,z) coords.
            double x1 = x0 - i1 + G3;
            double y1 = y0 - j1 + G3;
            double z1 = z0 - k1 + G3;

            // Offsets for third corner in (x,y,z).
            double x2 = x0 - i2 + F3;
            double y2 = y0 - j2 + F3;
            double z2 = z0 - k2 + F3;

            // Offsets for last corner in (x,y,z).
            double x3 = x0 - 0.5;
            double y3 = y0 - 0.5;
            double z3 = z0 - 0.5;

            // Work out the hashed gradient indices of the four simplex corners.
            int ii = i & 0xff;
            int jj = j & 0xff;
            int kk = k & 0xff;

            // Calculate the contribution from the four corners.
            double t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
            if (t0 > 0)
            {
                t0 *= t0;
                int gi0 = _random[ii + _random[jj + _random[kk]]] % 12;
                n0 = t0 * t0 * dot(Grad3[gi0], x0, y0, z0);
            }

            double t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
            if (t1 > 0)
            {
                t1 *= t1;
                int gi1 = _random[ii + i1 + _random[jj + j1 + _random[kk + k1]]] % 12;
                n1 = t1 * t1 * dot(Grad3[gi1], x1, y1, z1);
            }

            double t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
            if (t2 > 0)
            {
                t2 *= t2;
                int gi2 = _random[ii + i2 + _random[jj + j2 + _random[kk + k2]]] % 12;
                n2 = t2 * t2 * dot(Grad3[gi2], x2, y2, z2);
            }

            double t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
            if (t3 > 0)
            {
                t3 *= t3;
                int gi3 = _random[ii + 1 + _random[jj + 1 + _random[kk + 1]]] % 12;
                n3 = t3 * t3 * dot(Grad3[gi3], x3, y3, z3);
            }

            // Add contributions from each corner to get the final noise value.
            // The result is scaled to stay just inside [-1, 1].
            return static_cast<float>((n0 + n1 + n2 + n3) * 32.0);
        }

    private:
        static constexpr int RandomSize = 256;
        static constexpr double Sqrt3 = 1.7320508075688772935;

        // Skewing/unskewing factors for 3D.
        static constexpr double F3 = 1.0 / 3.0;
        static constexpr double G3 = 1.0 / 6.0;

        // Gradient vectors for 3D (pointing to the mid points of all edges of a unit cube).
        static constexpr int Grad3[12][3] = {
            {1, 1, 0}, {-1, 1, 0}, {1, -1, 0},
            {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
            {1, 0, -1}, {-1, 0, -1}, {0, 1, 1},
            {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};

        // Initial permutation table.
        static constexpr int Source[RandomSize] = {
            151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142,
            8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203,
            117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165,
            71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41,
            55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89,
            18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250,
            124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189,
            28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
            129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34,
            242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31,
            181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
            67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180};

        std::array<int, RandomSize * 2> _random{};

        void randomize(int seed)
        {
            // XOR each table entry with the seed's 4 bytes; they fold into one byte
            // (seed 0 -> fx 0 -> identity, i.e. the canonical table).
            const uint8_t fx = static_cast<uint8_t>((seed ^ (seed >> 8) ^ (seed >> 16) ^ (seed >> 24)) & 0xff);
            for (int i = 0; i < RandomSize; i++)
                _random[i + RandomSize] = _random[i] = Source[i] ^ fx;
        }

        static double dot(const int g[3], double x, double y, double z)
        {
            return g[0] * x + g[1] * y + g[2] * z;
        }

        static int fastFloor(double x)
        {
            return x >= 0 ? static_cast<int>(x) : static_cast<int>(x) - 1;
        }
    };
}
