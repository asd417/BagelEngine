#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// SMAA 1x — pass 2 of 3: blending-weight calculation. Faithful port of
// SMAABlendingWeightCalculationPS from iryoku/smaa (SMAA.hlsl), with DIAGONAL and CORNER
// detection disabled (the SMAA 1x base preset). For each edge pixel it searches along the edge,
// reads the crossing pattern, and looks up coverage areas in AreaTex → RGBA weights.
// Inputs (bindless): edges (RG, from pass 1), AreaTex (RG8), SearchTex (R8). Offsets/pixcoord
// are computed here instead of a dedicated vertex shader (we share the full-screen triangle vert).

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outWeights;

layout(set = 0, binding = 6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	uint edgesHandle;
	uint areaHandle;
	uint searchHandle;
} push;

const int   SMAA_MAX_SEARCH_STEPS      = 16;
const float SMAA_AREATEX_MAX_DISTANCE  = 16.0;
const vec2  SMAA_AREATEX_PIXEL_SIZE    = vec2(1.0 / 160.0, 1.0 / 560.0);
const float SMAA_AREATEX_SUBTEX_SIZE   = 1.0 / 7.0;
const vec2  SMAA_SEARCHTEX_SIZE        = vec2(66.0, 33.0);
const vec2  SMAA_SEARCHTEX_PACKED_SIZE = vec2(64.0, 16.0);

vec4 RT; // SMAA_RT_METRICS = (1/w, 1/h, w, h); set in main()

// LOD-0 samples (bypasses the shared sampler's mips/aniso; LUTs need exact level 0).
vec2  edgesLod(vec2 uv)  { return textureLod(samplerColor[push.edgesHandle],  uv, 0.0).rg; }
float searchLod(vec2 uv) { return textureLod(samplerColor[push.searchHandle], uv, 0.0).r; }
vec2  areaLod(vec2 uv)   { return textureLod(samplerColor[push.areaHandle],   uv, 0.0).rg; }

float SMAASearchLength(vec2 e, float offset) {
	vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);
	vec2 bias  = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);
	scale += vec2(-1.0,  1.0);
	bias  += vec2( 0.5, -0.5);
	scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
	bias  *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
	return searchLod(scale * e + bias);
}

float SMAASearchXLeft(vec2 texcoord, float end) {
	vec2 e = vec2(0.0, 1.0);
	for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
		if (!(texcoord.x > end && e.g > 0.8281 && e.r == 0.0)) break;
		e = edgesLod(texcoord);
		texcoord = -vec2(2.0, 0.0) * RT.xy + texcoord;
	}
	float offset = -(255.0 / 127.0) * SMAASearchLength(e, 0.0) + 3.25;
	return RT.x * offset + texcoord.x;
}

float SMAASearchXRight(vec2 texcoord, float end) {
	vec2 e = vec2(0.0, 1.0);
	for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
		if (!(texcoord.x < end && e.g > 0.8281 && e.r == 0.0)) break;
		e = edgesLod(texcoord);
		texcoord = vec2(2.0, 0.0) * RT.xy + texcoord;
	}
	float offset = -(255.0 / 127.0) * SMAASearchLength(e, 0.5) + 3.25;
	return -RT.x * offset + texcoord.x;
}

float SMAASearchYUp(vec2 texcoord, float end) {
	vec2 e = vec2(1.0, 0.0);
	for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
		if (!(texcoord.y > end && e.r > 0.8281 && e.g == 0.0)) break;
		e = edgesLod(texcoord);
		texcoord = -vec2(0.0, 2.0) * RT.xy + texcoord;
	}
	float offset = -(255.0 / 127.0) * SMAASearchLength(e.gr, 0.0) + 3.25;
	return RT.y * offset + texcoord.y;
}

float SMAASearchYDown(vec2 texcoord, float end) {
	vec2 e = vec2(1.0, 0.0);
	for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
		if (!(texcoord.y < end && e.r > 0.8281 && e.g == 0.0)) break;
		e = edgesLod(texcoord);
		texcoord = vec2(0.0, 2.0) * RT.xy + texcoord;
	}
	float offset = -(255.0 / 127.0) * SMAASearchLength(e.gr, 0.5) + 3.25;
	return -RT.y * offset + texcoord.y;
}

// Coverage-area lookup. e1/e2 are the crossing-edge values at the two ends; dist the distances.
vec2 SMAAArea(vec2 dist, float e1, float e2, float offset) {
	vec2 texcoord = SMAA_AREATEX_MAX_DISTANCE * round(4.0 * vec2(e1, e2)) + dist;
	texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;
	texcoord.y = SMAA_AREATEX_SUBTEX_SIZE * offset + texcoord.y;
	return areaLod(texcoord);
}

void main() {
	vec2 size = vec2(textureSize(samplerColor[push.edgesHandle], 0));
	RT = vec4(1.0 / size.x, 1.0 / size.y, size.x, size.y);

	vec2 pixcoord = fragUV * RT.zw;
	// Search offsets (normally produced by SMAABlendingWeightCalculationVS).
	vec4 offset0 = RT.xyxy * vec4(-0.25, -0.125,  1.25, -0.125) + fragUV.xyxy;
	vec4 offset1 = RT.xyxy * vec4(-0.125, -0.25, -0.125,  1.25) + fragUV.xyxy;
	vec4 offset2 = RT.xxyy * (vec4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS))
	             + vec4(offset0.xz, offset1.yw);

	vec4 weights = vec4(0.0);
	vec2 e = edgesLod(fragUV);

	if (e.g > 0.0) { // edge at north
		vec3 coords;
		coords.x = SMAASearchXLeft(offset0.xy, offset2.x);
		coords.y = offset1.y;
		float e1 = textureLod(samplerColor[push.edgesHandle], coords.xy, 0.0).r;
		coords.z = SMAASearchXRight(offset0.zw, offset2.y);
		vec2 d = abs(round(RT.zz * vec2(coords.x, coords.z) - pixcoord.xx));
		vec2 sqrt_d = sqrt(d);
		float e2 = textureLodOffset(samplerColor[push.edgesHandle], vec2(coords.z, coords.y), 0.0, ivec2(1, 0)).r;
		weights.rg = SMAAArea(sqrt_d, e1, e2, 0.0);
	}

	if (e.r > 0.0) { // edge at west
		vec3 coords;
		coords.y = SMAASearchYUp(offset1.xy, offset2.z);
		coords.x = offset0.x;
		float e1 = textureLod(samplerColor[push.edgesHandle], coords.xy, 0.0).g;
		coords.z = SMAASearchYDown(offset1.zw, offset2.w);
		vec2 d = abs(round(RT.ww * vec2(coords.y, coords.z) - pixcoord.yy));
		vec2 sqrt_d = sqrt(d);
		float e2 = textureLodOffset(samplerColor[push.edgesHandle], vec2(coords.x, coords.z), 0.0, ivec2(0, 1)).g;
		weights.ba = SMAAArea(sqrt_d, e1, e2, 0.0);
	}

	outWeights = weights;
}
