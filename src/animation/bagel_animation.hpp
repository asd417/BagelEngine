#pragma once
#include <vector>
#include <string>
#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Skeletal animation core: data model + the shared pose->palette pipeline.
//
// The whole system is built around one seam so authored clips, runtime blending, procedural
// motion, and inverse kinematics all share a path and the GPU stays agnostic:
//
//     sampleClip / blend ──► Pose (per-joint local TRS)
//                                  │  ◄── procedural & IK edit the Pose here
//                                  ▼
//                           resolveGlobals  (hierarchy walk: local -> model-space)
//                                  │  ◄── world-space IK can edit globals here
//                                  ▼
//                           globalsToPalette (× inverseBind)  ──► glm::mat4 palette
//                                  │
//                                  ▼
//        baked once at load  ──► resident SSBO region        (static clips)
//        written per frame   ──► dynamic SSBO region         (generative / IK)
//
// The shader only ever reads palette[animBase + jointIndex]; it cannot tell whether the row
// was baked at load or produced this frame by the CPU. That is what lets dynamic animation
// and IK drop in without touching the render path.

namespace bagel {

	// One joint's local transform as TRS. The pipeline works in TRS (not matrices) so
	// rotations slerp correctly and procedural/IK code can edit translation, rotation, and
	// scale independently before the hierarchy is flattened to matrices.
	struct JointTransform {
		glm::vec3 translation{ 0.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // identity (w, x, y, z)
		glm::vec3 scale{ 1.0f };
		glm::mat4 matrix() const;
	};

	// A full local pose: one JointTransform per joint, in skeleton joint-index order.
	using Pose = std::vector<JointTransform>;

	// Skeleton in joint-index space (0..jointCount-1). This is the index space the per-vertex
	// JOINTS_0 values and the GPU palette both use. restPose[j] is the bind/rest local TRS;
	// parents[j] is the parent joint index or -1 for a root; inverseBind[j] is the glTF inverse
	// bind matrix. Joint order is glTF skin.joints order; resolveGlobals is order-independent
	// (it resolves parents on demand), so no topological sort or vertex remap is required.
	struct SkeletonData {
		std::vector<JointTransform> restPose;
		std::vector<glm::mat4>      inverseBind;
		std::vector<int>            parents;
		uint32_t jointCount() const { return static_cast<uint32_t>(inverseBind.size()); }
		bool     empty()      const { return inverseBind.empty(); }
	};

	// glTF sampler interpolation modes.
	enum class Interp : uint8_t { STEP, LINEAR, CUBICSPLINE };
	// Which component of a JointTransform a channel drives.
	enum class AnimPath : uint8_t { TRANSLATION, ROTATION, SCALE };

	// A keyframe track. `times` are input times in seconds. `values` are outputs: xyz for
	// translation/scale, xyzw quaternion for rotation. CUBICSPLINE stores 3 values per key
	// (in-tangent, value, out-tangent), so values.size() == 3 * times.size() in that mode.
	struct AnimSampler {
		std::vector<float>     times;
		std::vector<glm::vec4> values;
		Interp                 interp = Interp::LINEAR;
	};

	// Binds a sampler to a target (joint, path). Channels targeting non-joint nodes or morph
	// weights are dropped at parse time, so `joint` is always a valid skeleton joint index.
	struct AnimChannel {
		int      joint   = -1;
		AnimPath path    = AnimPath::TRANSLATION;
		int      sampler = -1; // index into AnimationClip::samplers
	};

	// One named clip (glTF animation): a set of samplers and the channels that reference them.
	struct AnimationClip {
		std::string              name;
		float                    duration = 0.0f; // max keyframe time, seconds
		std::vector<AnimSampler> samplers;
		std::vector<AnimChannel> channels;
	};

	// ---- Shared pose pipeline ----------------------------------------------------------------

	// Sample `clip` at `time` (seconds, already wrapped/clamped by the caller) into `outPose`.
	// Starts from skel.restPose; joints with no channel keep their rest transform. outPose is
	// resized to skel.jointCount().
	void sampleClip(const SkeletonData& skel, const AnimationClip& clip, float time, Pose& outPose);

	// Hierarchy walk: local pose -> per-joint model-space (global) matrices. Order-independent
	// (parents resolved on demand). This is the seam world-space IK edits before palette build.
	void resolveGlobals(const SkeletonData& skel, const Pose& localPose, std::vector<glm::mat4>& outGlobals);

	// palette[j] = globals[j] * inverseBind[j]. `outPalette` must hold skel.jointCount() entries.
	// This is exactly what the GPU reads as palette[animBase + j].
	void globalsToPalette(const SkeletonData& skel, const std::vector<glm::mat4>& globals, glm::mat4* outPalette);

	// Convenience: localPose -> palette (resolveGlobals + globalsToPalette).
	void resolvePalette(const SkeletonData& skel, const Pose& localPose, glm::mat4* outPalette);

	// ---- Bake (static clips) -----------------------------------------------------------------

	// CPU-side baked palette set for all of a model's clips, sampled on a fixed time grid.
	// `matrices` is the resident-buffer contents, laid out by (clip, frame, joint):
	//   matrices[ (clipFrameBase[c] + frame) * jointCount + j ].
	// A draw selects a row with frameOffset(clip, frame) and pushes it as animBaseOffset.
	struct BakedAnimation {
		std::vector<glm::mat4> matrices;        // ready to upload into the resident palette SSBO
		std::vector<uint32_t>  clipFrameBase;   // per clip: first frame row (in frames)
		std::vector<uint32_t>  clipFrameCount;  // per clip: baked frame count
		uint32_t               jointCount = 0;
		float                  fps        = 30.0f;

		uint32_t frameOffset(uint32_t clip, uint32_t frame) const {
			return (clipFrameBase[clip] + frame) * jointCount;
		}
		uint32_t frameCount(uint32_t clip) const { return clipFrameCount[clip]; }
		size_t   matrixCount() const { return matrices.size(); }
	};

	// Bake every clip at `fps`. Done once at load; the result feeds the resident palette SSBO.
	// Dynamic/IK entities bypass this and write their palette per frame (see evaluatePoseLive).
	BakedAnimation bakeClips(const SkeletonData& skel, const std::vector<AnimationClip>& clips, float fps = 30.0f);

	// ---- Dynamic / generative / inverse kinematics seam --------------------------------------
	//
	// Everything below feeds the SAME resolveGlobals/globalsToPalette as the baker. The only
	// difference at runtime is the palette is written into a per-frame (dynamic) region of the
	// SSBO instead of read from the baked region.

	// Two-bone (limb) IK request, the common analytic case: place `tip` on `goal` by solving the
	// two rotations at `root` and `mid`, bending toward `pole`. Indices are skeleton joints.
	struct TwoBoneIK {
		int       root = -1;                  // upper joint  (e.g. upper arm / thigh)
		int       mid  = -1;                  // middle joint (e.g. forearm / shin)
		int       tip  = -1;                  // end joint    (e.g. hand / foot) — solved to reach goal
		glm::vec3 goalModelSpace{ 0.0f };     // target position in model space
		glm::vec3 poleModelSpace{ 0.0f };     // hint controlling the bend plane
		float     weight = 1.0f;              // 0 = no IK, 1 = full reach (blend with the sampled pose)
	};

	// Solve `ik` and write corrected local rotations for root/mid back into `pose`.
	// NOTE: STUB — the contract and call sites are in place so the dynamic pipeline is wired,
	// but the analytic solve is not implemented yet (the algorithm is documented in the .cpp).
	// Until implemented this is a no-op and leaves `pose` unchanged.
	void solveTwoBoneIK(const SkeletonData& skel, const TwoBoneIK& ik, Pose& pose);

	// Live (dynamic) evaluation entry point for generative animation and IK. Samples `clip` at
	// `time`, applies each IK request in order, then resolves straight to a palette the caller
	// uploads into the dynamic SSBO region for this frame. Bypasses the baked buffer entirely.
	// `outPalette` must hold skel.jointCount() entries.
	void evaluatePoseLive(const SkeletonData& skel, const AnimationClip& clip, float time,
	                      const std::vector<TwoBoneIK>& iks, glm::mat4* outPalette);
}
