#include "animation/bagel_animation.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace bagel {

	glm::mat4 JointTransform::matrix() const
	{
		glm::mat4 m = glm::translate(glm::mat4(1.0f), translation);
		m = m * glm::mat4_cast(rotation);
		m = glm::scale(m, scale);
		return m;
	}

	// Locate the keyframe interval [i0, i1] bracketing `t` and the fractional position between
	// them. Clamps to the endpoints (no extrapolation), matching glTF sampling at clip ends.
	static void findKeyframe(const std::vector<float>& times, float t, int& i0, int& i1, float& frac)
	{
		i0 = i1 = 0; frac = 0.0f;
		const int n = static_cast<int>(times.size());
		if (n == 0) return;
		if (t <= times.front()) { i0 = i1 = 0;       return; }
		if (t >= times.back())  { i0 = i1 = n - 1;    return; }
		int i = 0;
		while (i + 1 < n && times[i + 1] <= t) ++i;
		i0 = i; i1 = i + 1;
		const float d = times[i1] - times[i0];
		frac = d > 0.0f ? (t - times[i0]) / d : 0.0f;
	}

	void sampleClip(const SkeletonData& skel, const AnimationClip& clip, float time, Pose& outPose)
	{
		outPose = skel.restPose; // start from the rest pose; unanimated joints stay at rest
		const int jointCount = static_cast<int>(skel.jointCount());

		for (const AnimChannel& ch : clip.channels)
		{
			if (ch.joint < 0 || ch.joint >= jointCount) continue;
			if (ch.sampler < 0 || ch.sampler >= static_cast<int>(clip.samplers.size())) continue;
			const AnimSampler& s = clip.samplers[ch.sampler];
			if (s.times.empty() || s.values.empty()) continue;

			int i0, i1; float f;
			findKeyframe(s.times, time, i0, i1, f);

			// CUBICSPLINE packs (inTangent, value, outTangent) per key; we read the value slot
			// and interpolate linearly (tangents ignored — a documented approximation).
			const int stride = (s.interp == Interp::CUBICSPLINE) ? 3 : 1;
			const int vmid   = (s.interp == Interp::CUBICSPLINE) ? 1 : 0;
			const glm::vec4 a = s.values[static_cast<size_t>(i0) * stride + vmid];
			const glm::vec4 b = s.values[static_cast<size_t>(i1) * stride + vmid];

			JointTransform& jt = outPose[ch.joint];
			if (ch.path == AnimPath::ROTATION)
			{
				const glm::quat qa(a.w, a.x, a.y, a.z); // glTF stores xyzw; glm::quat takes (w,x,y,z)
				const glm::quat qb(b.w, b.x, b.y, b.z);
				jt.rotation = (s.interp == Interp::STEP) ? qa : glm::normalize(glm::slerp(qa, qb, f));
			}
			else
			{
				const glm::vec3 v = (s.interp == Interp::STEP) ? glm::vec3(a) : glm::mix(glm::vec3(a), glm::vec3(b), f);
				if (ch.path == AnimPath::TRANSLATION) jt.translation = v;
				else                                  jt.scale       = v;
			}
		}
	}

	void resolveGlobals(const SkeletonData& skel, const Pose& localPose, std::vector<glm::mat4>& outGlobals)
	{
		const int n = static_cast<int>(skel.jointCount());
		outGlobals.assign(n, glm::mat4(1.0f));
		std::vector<char> done(n, 0);

		// Resolve a joint's model-space matrix, recursing into its parent first. Order-independent:
		// joints may be stored in any order; each global is computed once and memoized.
		std::function<const glm::mat4&(int)> resolve = [&](int j) -> const glm::mat4& {
			if (!done[j])
			{
				const glm::mat4 local = (j < static_cast<int>(localPose.size())) ? localPose[j].matrix() : glm::mat4(1.0f);
				const int p = skel.parents[j];
				outGlobals[j] = (p >= 0 && p < n) ? resolve(p) * local : local;
				done[j] = 1;
			}
			return outGlobals[j];
		};
		for (int j = 0; j < n; ++j) resolve(j);
	}

	void globalsToPalette(const SkeletonData& skel, const std::vector<glm::mat4>& globals, glm::mat4* outPalette)
	{
		const int n = static_cast<int>(skel.jointCount());
		for (int j = 0; j < n; ++j)
			outPalette[j] = globals[j] * skel.inverseBind[j];
	}

	void resolvePalette(const SkeletonData& skel, const Pose& localPose, glm::mat4* outPalette)
	{
		std::vector<glm::mat4> globals;
		resolveGlobals(skel, localPose, globals);
		globalsToPalette(skel, globals, outPalette);
	}

	BakedAnimation bakeClips(const SkeletonData& skel, const std::vector<AnimationClip>& clips, float fps)
	{
		BakedAnimation out;
		out.jointCount = skel.jointCount();
		out.fps        = fps > 0.0f ? fps : 30.0f;
		out.clipFrameBase.resize(clips.size());
		out.clipFrameCount.resize(clips.size());

		// Frame layout: each clip gets ceil(duration*fps)+1 frames (inclusive of the endpoint).
		uint32_t totalFrames = 0;
		for (size_t c = 0; c < clips.size(); ++c)
		{
			const uint32_t frames = static_cast<uint32_t>(std::ceil(clips[c].duration * out.fps)) + 1;
			out.clipFrameBase[c]  = totalFrames;
			out.clipFrameCount[c] = frames;
			totalFrames += frames;
		}
		out.matrices.assign(static_cast<size_t>(totalFrames) * out.jointCount, glm::mat4(1.0f));
		if (out.jointCount == 0) return out;

		Pose pose;
		for (size_t c = 0; c < clips.size(); ++c)
			for (uint32_t f = 0; f < out.clipFrameCount[c]; ++f)
			{
				const float t = f / out.fps;
				sampleClip(skel, clips[c], t, pose);
				resolvePalette(skel, pose, &out.matrices[static_cast<size_t>(out.clipFrameBase[c] + f) * out.jointCount]);
			}
		return out;
	}

	void solveTwoBoneIK(const SkeletonData& /*skel*/, const TwoBoneIK& /*ik*/, Pose& /*pose*/)
	{
		// STUB — intentionally a no-op for now. The seam, data, and call site exist so the
		// dynamic pipeline is fully wired; only the analytic solve below is outstanding.
		//
		// Analytic two-bone IK (to implement):
		//   1. resolveGlobals(skel, pose, globals); read world positions
		//        A = globals[root][3], B = globals[mid][3], C = globals[tip][3].
		//   2. lab = |B-A|, lbc = |C-B|, lat = clamp(|goal-A|, eps, lab+lbc-eps).
		//   3. Interior angles via law of cosines:
		//        at A: acos((lab^2 + lat^2 - lbc^2) / (2*lab*lat))
		//        at B: acos((lab^2 + lbc^2 - lat^2) / (2*lab*lbc))
		//      and the current angles from the present A/B/C directions.
		//   4. Bend axis = normalize(cross(C-A, pole-A)); reach axis = normalize(cross(C-A, goal-A)).
		//   5. Apply the (target - current) deltas to root and mid as WORLD rotations, then convert
		//      back to LOCAL: local = inverse(parentGlobalRot) * worldRot, and write into pose,
		//      blended by ik.weight. (Convert each joint's existing global rotation, not the matrix.)
	}

	void evaluatePoseLive(const SkeletonData& skel, const AnimationClip& clip, float time,
	                      const std::vector<TwoBoneIK>& iks, glm::mat4* outPalette)
	{
		Pose pose;
		sampleClip(skel, clip, time, pose);
		for (const TwoBoneIK& ik : iks)
			solveTwoBoneIK(skel, ik, pose); // no-op until implemented; pipeline is otherwise live
		resolvePalette(skel, pose, outPalette);
	}
}
