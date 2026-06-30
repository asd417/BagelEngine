// Procedural noise shared with the CPU terrain. MUST stay bit-consistent with
// bagel_math.hpp::perlin and planet_terrain.hpp::baseHeight, or the fragment-shader
// surface drifts from the displaced geometry. GLSL uint matches C++ uint32_t
// (32-bit wraparound), and uint(int) reinterprets two's complement like the C++ cast.
#ifndef NOISE_GLSL
#define NOISE_GLSL

// Ken Perlin's 12 gradient directions (+4 dupes). dot(GRAD[h&15], (x,y,z)) reproduces
// bagel_math.hpp's gradDot switch exactly (same per-case vectors, same order).
const vec3 PERLIN_GRAD[16] = vec3[16](
    vec3( 1, 1, 0), vec3(-1, 1, 0), vec3( 1,-1, 0), vec3(-1,-1, 0),
    vec3( 1, 0, 1), vec3(-1, 0, 1), vec3( 1, 0,-1), vec3(-1, 0,-1),
    vec3( 0, 1, 1), vec3( 0,-1, 1), vec3( 0, 1,-1), vec3( 0,-1,-1),
    vec3( 1, 1, 0), vec3( 0,-1, 1), vec3(-1, 1, 0), vec3( 0,-1,-1)
);

float perlinFade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
// d/dt of perlinFade: 30 t^2 (t-1)^2. Needed by the analytic-gradient variant below.
float perlinFadeD(float t) { return 30.0 * t * t * (t * (t - 2.0) + 1.0); }

uint perlinHash(ivec3 c, uint seed) {
    uint h = uint(c.x) * 0x8DA6B343u ^ uint(c.y) * 0xD8163841u ^ uint(c.z) * 0xCB1AB31Fu ^ seed * 0x9E3779B1u;
    h ^= h >> 15u;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12u;
    return h;
}
vec3  perlinGradVec(ivec3 c, uint seed) { return PERLIN_GRAD[int(perlinHash(c, seed) & 15u)]; }
float perlinGradDot(int ix, int iy, int iz, float x, float y, float z, uint seed) {
    return dot(perlinGradVec(ivec3(ix, iy, iz), seed), vec3(x, y, z));
}

float perlin(vec3 p, uint seed) {
    vec3 pf = floor(p);
    ivec3 i = ivec3(pf);
    vec3 f = p - pf;
    vec3 u = vec3(perlinFade(f.x), perlinFade(f.y), perlinFade(f.z));

    float n000 = perlinGradDot(i.x,   i.y,   i.z,   f.x,       f.y,       f.z,       seed);
    float n100 = perlinGradDot(i.x+1, i.y,   i.z,   f.x-1.0,   f.y,       f.z,       seed);
    float n010 = perlinGradDot(i.x,   i.y+1, i.z,   f.x,       f.y-1.0,   f.z,       seed);
    float n110 = perlinGradDot(i.x+1, i.y+1, i.z,   f.x-1.0,   f.y-1.0,   f.z,       seed);
    float n001 = perlinGradDot(i.x,   i.y,   i.z+1, f.x,       f.y,       f.z-1.0,   seed);
    float n101 = perlinGradDot(i.x+1, i.y,   i.z+1, f.x-1.0,   f.y,       f.z-1.0,   seed);
    float n011 = perlinGradDot(i.x,   i.y+1, i.z+1, f.x,       f.y-1.0,   f.z-1.0,   seed);
    float n111 = perlinGradDot(i.x+1, i.y+1, i.z+1, f.x-1.0,   f.y-1.0,   f.z-1.0,   seed);

    float nx00 = mix(n000, n100, u.x), nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x), nx11 = mix(n011, n111, u.x);
    float nxy0 = mix(nx00, nx10, u.y), nxy1 = mix(nx01, nx11, u.y);
    return mix(nxy0, nxy1, u.z); // ~[-1, 1]
}

// ---- 4D Perlin (for animated ocean waves: xyz = world pos, w = time) -----------------
// Ken Perlin's 32 4D gradient directions (3 of 4 axes ±1). dot with the offset vector.
const vec4 PERLIN_GRAD4[32] = vec4[32](
    vec4(0,1,1,1),  vec4(0,1,1,-1),  vec4(0,1,-1,1),  vec4(0,1,-1,-1),
    vec4(0,-1,1,1), vec4(0,-1,1,-1), vec4(0,-1,-1,1), vec4(0,-1,-1,-1),
    vec4(1,0,1,1),  vec4(1,0,1,-1),  vec4(1,0,-1,1),  vec4(1,0,-1,-1),
    vec4(-1,0,1,1), vec4(-1,0,1,-1), vec4(-1,0,-1,1), vec4(-1,0,-1,-1),
    vec4(1,1,0,1),  vec4(1,1,0,-1),  vec4(1,-1,0,1),  vec4(1,-1,0,-1),
    vec4(-1,1,0,1), vec4(-1,1,0,-1), vec4(-1,-1,0,1), vec4(-1,-1,0,-1),
    vec4(1,1,1,0),  vec4(1,1,-1,0),  vec4(1,-1,1,0),  vec4(1,-1,-1,0),
    vec4(-1,1,1,0), vec4(-1,1,-1,0), vec4(-1,-1,1,0), vec4(-1,-1,-1,0)
);

uint perlin4Hash(ivec4 c, uint seed) {
    uint h = uint(c.x)*0x8DA6B343u ^ uint(c.y)*0xD8163841u ^ uint(c.z)*0xCB1AB31Fu
           ^ uint(c.w)*0x165667B1u ^ seed*0x9E3779B1u;
    h ^= h >> 15u; h *= 0x2C1B3C6Du; h ^= h >> 12u;
    return h;
}
vec4  perlin4GradVec(ivec4 c, uint seed) { return PERLIN_GRAD4[int(perlin4Hash(c, seed) & 31u)]; }
float perlin4Grad(ivec4 i, vec4 rel, uint seed) { return dot(perlin4GradVec(i, seed), rel); }

float perlin4(vec4 p, uint seed) {
    vec4 pf = floor(p);
    ivec4 i = ivec4(pf);
    vec4 f = p - pf;
    vec4 u = vec4(perlinFade(f.x), perlinFade(f.y), perlinFade(f.z), perlinFade(f.w));
    float n[16];
    for (int c = 0; c < 16; ++c) {
        ivec4 o = ivec4(c & 1, (c >> 1) & 1, (c >> 2) & 1, (c >> 3) & 1);
        n[c] = perlin4Grad(i + o, f - vec4(o), seed);
    }
    // quadrilinear blend: x, then y, then z, then w
    float x0=mix(n[0],n[1],u.x),  x1=mix(n[2],n[3],u.x),  x2=mix(n[4],n[5],u.x),  x3=mix(n[6],n[7],u.x);
    float x4=mix(n[8],n[9],u.x),  x5=mix(n[10],n[11],u.x),x6=mix(n[12],n[13],u.x),x7=mix(n[14],n[15],u.x);
    float y0=mix(x0,x1,u.y), y1=mix(x2,x3,u.y), y2=mix(x4,x5,u.y), y3=mix(x6,x7,u.y);
    float z0=mix(y0,y1,u.z), z1=mix(y2,y3,u.z);
    return mix(z0, z1, u.w); // ~[-1,1]
}

// 4D Perlin value + SPATIAL gradient (xyz only; the w/time partial isn't needed for a
// surface normal). Returns vec4(value, d/dx, d/dy, d/dz). One eval replaces 3 finite-diff
// perlin4 calls, and the gradient is analytic so a normal built from it has no tangent-frame
// dependence (no pole seam).
vec4 perlin4D(vec4 p, uint seed) {
    vec4 pf = floor(p);
    ivec4 i = ivec4(pf);
    vec4 f = p - pf;
    vec4 u  = vec4(perlinFade(f.x),  perlinFade(f.y),  perlinFade(f.z),  perlinFade(f.w));
    vec4 du = vec4(perlinFadeD(f.x), perlinFadeD(f.y), perlinFadeD(f.z), perlinFadeD(f.w));

    vec4 g[16]; float n[16];
    for (int c = 0; c < 16; ++c) {
        ivec4 o = ivec4(c & 1, (c >> 1) & 1, (c >> 2) & 1, (c >> 3) & 1);
        g[c] = perlin4GradVec(i + o, seed);
        n[c] = dot(g[c], f - vec4(o));
    }
    float x0=mix(n[0],n[1],u.x),  x1=mix(n[2],n[3],u.x),  x2=mix(n[4],n[5],u.x),  x3=mix(n[6],n[7],u.x);
    float x4=mix(n[8],n[9],u.x),  x5=mix(n[10],n[11],u.x),x6=mix(n[12],n[13],u.x),x7=mix(n[14],n[15],u.x);
    float y0=mix(x0,x1,u.y), y1=mix(x2,x3,u.y), y2=mix(x4,x5,u.y), y3=mix(x6,x7,u.y);
    float z0=mix(y0,y1,u.z), z1=mix(y2,y3,u.z);
    float val = mix(z0, z1, u.w);

    vec3 grad = vec3(0.0);
    for (int c = 0; c < 16; ++c) {
        vec4 a = vec4(c & 1, (c >> 1) & 1, (c >> 2) & 1, (c >> 3) & 1);
        vec4 W = mix(1.0 - u, u, a);                 // per-axis blend weight
        vec4 s = mix(vec4(-1.0), vec4(1.0), a) * du; // d(weight axis)/df
        grad.x += n[c] * (s.x * W.y * W.z * W.w);
        grad.y += n[c] * (W.x * s.y * W.z * W.w);
        grad.z += n[c] * (W.x * W.y * s.z * W.w);
        grad   += (W.x * W.y * W.z * W.w) * g[c].xyz;
    }
    return vec4(val, grad);
}

// fBm over perlin, matching planet_terrain.hpp::baseHeight's loop (seed + octave per band).
float fbm(vec3 dir, float freq, int octaves, float lacunarity, float gain, uint seed) {
    float sum = 0.0, amp = 1.0, norm = 0.0, f = freq;
    for (int o = 0; o < octaves; ++o) {
        sum  += amp * perlin(dir * f, seed + uint(o));
        norm += amp;
        f    *= lacunarity;
        amp  *= gain;
    }
    return (norm > 0.0) ? sum / norm : 0.0; // ~[-1, 1]
}

// ---- Perlin value + analytic gradient (Inigo Quilez's derivative noise) ---------------
// Returns vec4(value, dvalue/dp). The .x value is computed by the SAME nested-mix as
// perlin() above, so it stays bit-identical (and thus matches the CPU height). The
// gradient lets callers get a surface normal from ONE evaluation instead of 3 finite
// differences. CPU mirror: bagel_math.hpp::perlinD.
vec4 perlinD(vec3 p, uint seed) {
    vec3 pf = floor(p);
    ivec3 i = ivec3(pf);
    vec3 f = p - pf;
    vec3 u  = vec3(perlinFade(f.x),  perlinFade(f.y),  perlinFade(f.z));
    vec3 du = vec3(perlinFadeD(f.x), perlinFadeD(f.y), perlinFadeD(f.z));

    vec3 g[8]; float n[8];
    for (int c = 0; c < 8; ++c) {
        ivec3 o = ivec3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
        g[c] = perlinGradVec(i + o, seed);
        n[c] = dot(g[c], f - vec3(o));
    }
    // value: identical nested trilinear to perlin() (n[0..7] = n000,n100,n010,n110,n001,...)
    float nx00 = mix(n[0], n[1], u.x), nx10 = mix(n[2], n[3], u.x);
    float nx01 = mix(n[4], n[5], u.x), nx11 = mix(n[6], n[7], u.x);
    float nxy0 = mix(nx00, nx10, u.y), nxy1 = mix(nx01, nx11, u.y);
    float val  = mix(nxy0, nxy1, u.z);

    // gradient = Sum_c [ n_c * d(weight_c)/df  +  weight_c * g_c ]
    vec3 grad = vec3(0.0);
    for (int c = 0; c < 8; ++c) {
        vec3 a  = vec3(c & 1, (c >> 1) & 1, (c >> 2) & 1);       // corner bits
        vec3 W  = mix(1.0 - u, u, a);                            // per-axis blend weight
        vec3 s  = mix(vec3(-1.0), vec3(1.0), a) * du;            // d(weight axis)/df
        grad += n[c] * vec3(s.x * W.y * W.z, W.x * s.y * W.z, W.x * W.y * s.z);
        grad += (W.x * W.y * W.z) * g[c];
    }
    return vec4(val, grad);
}

// fBm value + gradient (chain rule: d/ddir of perlin(dir*f) = f * grad). Mirrors fbm()'s
// value exactly; CPU mirror: planet_terrain.hpp::baseHeightGrad.
vec4 fbmD(vec3 dir, float freq, int octaves, float lacunarity, float gain, uint seed) {
    float sum = 0.0, amp = 1.0, norm = 0.0, f = freq;
    vec3 grad = vec3(0.0);
    for (int o = 0; o < octaves; ++o) {
        vec4 pd = perlinD(dir * f, seed + uint(o));
        sum  += amp * pd.x;
        grad += amp * f * pd.yzw;
        norm += amp;
        f    *= lacunarity;
        amp  *= gain;
    }
    return (norm > 0.0) ? vec4(sum, grad) / norm : vec4(0.0);
}

#endif // NOISE_GLSL
