#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// SMAA 1x — pass 3 of 3: neighborhood blending. Port of SMAANeighborhoodBlendingPS from
// iryoku/smaa. Reads the LDR color (composite) + the blend-weights, and bilinearly blends each
// pixel toward the chosen neighbor by its weights — the actual anti-aliasing. Writes the
// swapchain (this pass IS the present). If `enabled` is 0, it passes the color through unchanged.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 6) uniform sampler2D samplerColor[];

layout(push_constant) uniform Push {
	uint colorHandle;  // composite LDR image
	uint weightHandle; // blend weights from pass 2
	uint enabled;      // 0 = passthrough (e.g. SMAA off / debug views), 1 = blend
} push;

void main() {
	vec4 color = textureLod(samplerColor[push.colorHandle], fragUV, 0.0);
	if (push.enabled == 0u) {
		outColor = color;
		return;
	}

	vec2 size = vec2(textureSize(samplerColor[push.colorHandle], 0));
	vec4 RT = vec4(1.0 / size.x, 1.0 / size.y, size.x, size.y);

	// Neighbor weight fetches (offsets: right and bottom).
	vec4 offset = RT.xyxy * vec4(1.0, 0.0, 0.0, 1.0) + fragUV.xyxy;
	vec4 a;
	a.x  = textureLod(samplerColor[push.weightHandle], offset.xy, 0.0).a; // right pixel
	a.y  = textureLod(samplerColor[push.weightHandle], offset.zw, 0.0).g; // bottom pixel
	a.wz = textureLod(samplerColor[push.weightHandle], fragUV,    0.0).xz; // this pixel

	// No blending weight? Keep the original color.
	if (dot(a, vec4(1.0)) < 1e-5) {
		outColor = color;
		return;
	}

	bool h = max(a.x, a.z) > max(a.y, a.w); // horizontal blend dominates?

	vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);
	vec2 blendingWeight = a.yw;
	blendingOffset = mix(blendingOffset, vec4(a.x, 0.0, a.z, 0.0), bvec4(h));
	blendingWeight = mix(blendingWeight, a.xz, bvec2(h));
	blendingWeight /= dot(blendingWeight, vec2(1.0));

	vec4 blendingCoord = blendingOffset * vec4(RT.xy, -RT.xy) + fragUV.xyxy;
	vec4 blended  = blendingWeight.x * textureLod(samplerColor[push.colorHandle], blendingCoord.xy, 0.0);
	blended      += blendingWeight.y * textureLod(samplerColor[push.colorHandle], blendingCoord.zw, 0.0);
	outColor = blended;
}
