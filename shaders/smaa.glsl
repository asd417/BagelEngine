// Shared SMAA 1x luma edge detection (iryoku/smaa). Used by the dedicated edge pass
// (smaa_edge.frag) and by the r_drawmode debug view in deferred_lighting.frag, so both compute
// identical edges. The includer must declare the bindless texture array before including:
//   layout(set = 0, binding = 6) uniform sampler2D samplerColor[];
#ifndef SMAA_GLSL
#define SMAA_GLSL

// Edge contrast threshold (SMAA default 0.1 over the [0,1] luma range; lower = more edges).
//const float SMAA_THRESHOLD = 0.05;
// Local contrast adaptation — suppress an edge dwarfed by a neighbor's contrast (kills doubles).
//const float SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR = 2.0;
const vec3  SMAA_LUMA_WEIGHTS = vec3(0.2126, 0.7152, 0.0722); // Rec.709

// Point-sample (texelFetch — SMAA requires point sampling) the luma of bindless texture `h`.
float smaaLumaAt(uint h, ivec2 p, ivec2 size) {
	p = clamp(p, ivec2(0), size - ivec2(1)); // guard the screen border
	return dot(texelFetch(samplerColor[h], p, 0).rgb, SMAA_LUMA_WEIGHTS);
}

// SMAALumaEdgeDetection: returns RG edges (R = west edge present, G = north edge present);
// vec2(0) where there is no edge.
vec2 smaaLumaEdges(uint h, vec2 uv, float smaaThreshold, float smaaLocalContrastAdapt) {
	const ivec2 size = textureSize(samplerColor[h], 0);
	const ivec2 p    = clamp(ivec2(uv * vec2(size)), ivec2(0), size - ivec2(1));

	float L     = smaaLumaAt(h, p,                 size);
	float Lleft = smaaLumaAt(h, p + ivec2(-1, 0),  size);
	float Ltop  = smaaLumaAt(h, p + ivec2( 0, -1), size);

	vec4 delta;
	delta.xy = abs(L - vec2(Lleft, Ltop));
	vec2 edges = step(vec2(smaaThreshold), delta.xy);
	if (edges.x + edges.y == 0.0)
		return vec2(0.0);

	// Local contrast adaptation: east/south and the second west/north ring.
	float Lright    = smaaLumaAt(h, p + ivec2( 1, 0),  size);
	float Lbottom   = smaaLumaAt(h, p + ivec2( 0, 1),  size);
	delta.zw = abs(L - vec2(Lright, Lbottom));
	vec2 maxDelta = max(delta.xy, delta.zw);

	float Lleftleft = smaaLumaAt(h, p + ivec2(-2, 0),  size);
	float Ltoptop   = smaaLumaAt(h, p + ivec2( 0, -2), size);
	delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));
	maxDelta = max(maxDelta, delta.zw);

	float finalDelta = max(maxDelta.x, maxDelta.y);
	edges *= step(vec2(finalDelta), smaaLocalContrastAdapt * delta.xy);
	return edges;
}

// Point-sample the rgb of bindless texture `h`.
vec3 smaaColorAt(uint h, ivec2 p, ivec2 size) {
	p = clamp(p, ivec2(0), size - ivec2(1));
	return texelFetch(samplerColor[h], p, 0).rgb;
}

// SMAAColorEdgeDetection: like luma, but the per-direction delta is the max absolute difference
// across the RGB channels — catches chroma edges luma misses (e.g. equal-luma color changes), at
// roughly 2x the sample cost. Returns RG edges; vec2(0) where there is no edge.
vec2 smaaColorEdges(uint h, vec2 uv, float smaaThreshold, float smaaLocalContrastAdapt) {
	const ivec2 size = textureSize(samplerColor[h], 0);
	const ivec2 p    = clamp(ivec2(uv * vec2(size)), ivec2(0), size - ivec2(1));

	vec3 C     = smaaColorAt(h, p,                 size);
	vec3 Cleft = smaaColorAt(h, p + ivec2(-1, 0),  size);
	vec3 Ctop  = smaaColorAt(h, p + ivec2( 0, -1), size);

	vec4 delta;
	vec3 t = abs(C - Cleft); delta.x = max(max(t.r, t.g), t.b);
	t      = abs(C - Ctop);  delta.y = max(max(t.r, t.g), t.b);
	vec2 edges = step(vec2(smaaThreshold), delta.xy);
	if (edges.x + edges.y == 0.0)
		return vec2(0.0);

	vec3 Cright  = smaaColorAt(h, p + ivec2(1, 0), size); t = abs(C - Cright);  delta.z = max(max(t.r, t.g), t.b);
	vec3 Cbottom = smaaColorAt(h, p + ivec2(0, 1), size); t = abs(C - Cbottom); delta.w = max(max(t.r, t.g), t.b);
	vec2 maxDelta = max(delta.xy, delta.zw);

	vec3 Cll = smaaColorAt(h, p + ivec2(-2, 0), size); t = abs(Cleft - Cll); delta.z = max(max(t.r, t.g), t.b);
	vec3 Ctt = smaaColorAt(h, p + ivec2(0, -2), size); t = abs(Ctop - Ctt);  delta.w = max(max(t.r, t.g), t.b);
	maxDelta = max(maxDelta, delta.zw);

	float finalDelta = max(maxDelta.x, maxDelta.y);
	edges *= step(vec2(finalDelta), smaaLocalContrastAdapt * delta.xy);
	return edges;
}

float smaaDepthAt(sampler2D depthTex, ivec2 p, ivec2 size) {
	p = clamp(p, ivec2(0), size - ivec2(1)); // texelFetch needs INTEGER coords
	return texelFetch(depthTex, p, 0).r;
}

// Linearize ZERO_TO_ONE non-linear hardware depth d to [0,1] over [near, far]. Raw perspective
// depth is crammed near 1.0, so in-surface deltas are ~0 and a single threshold is useless;
// linearizing spreads it so the threshold is meaningful across the scene. near/far are
// reconstructed from the projection matrix by the caller.
float smaaLinearize01(float d, float nearP, float farP) {
	float eye = nearP * farP / (farP + d * (nearP - farP)); // view-space distance [near, far]
	return (eye - nearP) / (farP - nearP);
}

// SMAADepthEdgeDetection: cheapest mode — compares LINEARIZED depth against west/north neighbors.
// Geometry-only (misses interior/texture edges), no local contrast adaptation.
vec2 smaaDepthEdges(sampler2D depthTex, vec2 uv, float depthThreshold, float nearP, float farP) {
	const ivec2 size = textureSize(depthTex, 0);
	const ivec2 p    = clamp(ivec2(uv * vec2(size)), ivec2(0), size - ivec2(1));

	float D     = smaaLinearize01(smaaDepthAt(depthTex, p,                size), nearP, farP);
	float Dleft = smaaLinearize01(smaaDepthAt(depthTex, p + ivec2(-1, 0), size), nearP, farP);
	float Dtop  = smaaLinearize01(smaaDepthAt(depthTex, p + ivec2( 0, -1), size), nearP, farP);

	vec2 delta = abs(D - vec2(Dleft, Dtop));
	return step(vec2(depthThreshold), delta);
}

#endif // SMAA_GLSL
