#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

// SMAA 1x — pass 1 of 3: luma edge detection. Reads the composite (post-tonemap, LDR) image
// selected by push.inputHandle and writes an RG edges target: R = west edge, G = north edge.
// Non-edge pixels are discarded, so the edges target must be cleared to 0 each frame.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec2 outEdges;

// Deferred depth (G-buffer), for the depth edge-detection mode.
layout(set = 0, binding = 0) uniform sampler2D gDepth;
// Bindless texture array; the color image is selected by push.inputHandle (luma/color modes).
layout(set = 0, binding = 6) uniform sampler2D samplerColor[];

#include "ubo.glsl"   // GlobalUBO (projection matrix) for depth linearization
#include "smaa.glsl"

layout(push_constant) uniform Push {
	uint inputHandle; // bindless handle of the color image to edge-detect (luma/color modes)
	float threshold;
	float localContrastAdapt;
	uint method;      // 0 = luma, 1 = color, 2 = depth
} push;

void main() {
	vec2 edges;
	if (push.method == 0u)
		edges = smaaColorEdges(push.inputHandle, fragUV, push.threshold, push.localContrastAdapt);
	else if (push.method == 1u) {
		// Reconstruct near/far from the perspective projection (GLM ZERO_TO_ONE) to linearize depth.
		float P22 = ubo.projectionMatrix[2][2];
		float P32 = ubo.projectionMatrix[3][2];
		float nearP = P32 / P22;
		float farP  = P32 / (P22 + 1.0);
		edges = smaaDepthEdges(gDepth, fragUV, push.threshold, nearP, farP);
	}
	else if (push.method == 2u) {
		edges = smaaLumaEdges(push.inputHandle, fragUV, push.threshold, push.localContrastAdapt);
	}
	else
	{
		float P22 = ubo.projectionMatrix[2][2];
		float P32 = ubo.projectionMatrix[3][2];
		float nearP = P32 / P22;
		float farP  = P32 / (P22 + 1.0);
		edges = max(smaaColorEdges(push.inputHandle, fragUV, 0.22f, 2.0f), smaaDepthEdges(gDepth, fragUV, 0.0005f, nearP, farP));
	}
	if (edges.x + edges.y == 0.0)
		discard; // not an edge — leave the cleared 0
	outEdges = edges;
}
