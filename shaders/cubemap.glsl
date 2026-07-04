// Cube <-> sphere addressing — the GPU half of a CPU/GPU shared mapping.
// MUST stay bit-consistent with src/math/planet_cubemap.hpp::dirToFaceUV (the same
// discipline noise.glsl follows for perlin), or CPU and GPU cube addressing drift
// apart. Standard GL cube convention: major axis selects one of six faces
// (0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z); the other two components / |major| give uv in
// [-1,1] -> [0,1].
#ifndef CUBEMAP_GLSL
#define CUBEMAP_GLSL

void dirToFaceUV(vec3 d, out int face, out vec2 uv) {
    float ax = abs(d.x), ay = abs(d.y), az = abs(d.z);
    float ma, sc, tc;
    if (ax >= ay && ax >= az) {
        if (d.x >= 0.0) { face = 0; sc = -d.z; tc = -d.y; }
        else            { face = 1; sc =  d.z; tc = -d.y; }
        ma = ax;
    } else if (ay >= az) {
        if (d.y >= 0.0) { face = 2; sc =  d.x; tc =  d.z; }
        else            { face = 3; sc =  d.x; tc = -d.z; }
        ma = ay;
    } else {
        if (d.z >= 0.0) { face = 4; sc =  d.x; tc = -d.y; }
        else            { face = 5; sc = -d.x; tc = -d.y; }
        ma = az;
    }
    uv.x = 0.5 * (sc / ma + 1.0);
    uv.y = 0.5 * (tc / ma + 1.0);
}

#endif // CUBEMAP_GLSL
