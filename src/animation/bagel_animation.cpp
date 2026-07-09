#include "animation/bagel_animation.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

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

	// Resolve joint `j`'s model-space matrix, recursing into its parent first. Order-independent:
	// joints may be stored in any order; each global is computed once and memoized in outGlobals,
	// with `done` tracking which are resolved. outGlobals is pre-sized, so the returned reference
	// stays valid across the recursion.
	static const glm::mat4& resolveJoint(const SkeletonData& skel, const Pose& localPose,
										 std::vector<glm::mat4>& outGlobals, std::vector<char>& done, int j)
	{
		if (!done[j])
		{
			const int n = static_cast<int>(outGlobals.size());
			const glm::mat4 local = (j < static_cast<int>(localPose.size())) ? localPose[j].matrix() : glm::mat4(1.0f);
			const int p = skel.parents[j];
			outGlobals[j] = (p >= 0 && p < n) ? resolveJoint(skel, localPose, outGlobals, done, p) * local : local;
			done[j] = 1;
		}
		return outGlobals[j];
	}

	void resolveGlobals(const SkeletonData& skel, const Pose& localPose, std::vector<glm::mat4>& outGlobals)
	{
		const int n = static_cast<int>(skel.jointCount());
		outGlobals.assign(n, glm::mat4(1.0f));
		std::vector<char> done(n, 0);

		for (int j = 0; j < n; ++j) resolveJoint(skel, localPose, outGlobals, done, j);
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
		out.fps        = fps > 0.0f ? fps : 60.0f;
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
				const float t = static_cast<float>(f) / out.fps;
				sampleClip(skel, clips[c], t, pose);
				resolvePalette(skel, pose, &out.matrices[static_cast<size_t>(out.clipFrameBase[c] + f) * out.jointCount]);
			}
		return out;
	}

	void solveTwoBoneIK(const SkeletonData& skel, const TwoBoneIK& ik, Pose& pose)
	{
		const int n = static_cast<int>(skel.jointCount());
		if (ik.thigh < 0 || ik.shin < 0 || ik.foot < 0) return;
		if (ik.thigh >= n || ik.shin >= n || ik.foot >= n) return;
		if (ik.weight <= 0.0f) return;

		const glm::quat origThigh = pose[ik.thigh].rotation; // for weight blend
		const glm::quat origShin  = pose[ik.shin].rotation;

		auto safeNorm = [](const glm::vec3& v) { float l = glm::length(v); return l > 1e-6f ? v / l : glm::vec3(0.0f); };
		// World rotation of a joint's parent in model space (identity for a root joint).
		auto parentRot = [&](const std::vector<glm::mat4>& g, int joint) -> glm::quat {
			const int p = skel.parents[joint];
			return (p >= 0 && p < n) ? glm::quat_cast(glm::mat3(g[p])) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		};
		// Apply a world-space rotation Qw about the joint's pivot by editing its LOCAL rotation:
		//   newLocal = parentRot^-1 * Qw * jointWorldRot.
		auto applyWorld = [&](const std::vector<glm::mat4>& g, int joint, const glm::quat& Qw) {
			const glm::quat world = glm::quat_cast(glm::mat3(g[joint]));
			pose[joint].rotation = glm::normalize(glm::inverse(parentRot(g, joint)) * Qw * world);
		};

		const float eps = 1e-4f;
		std::vector<glm::mat4> g;
		resolveGlobals(skel, pose, g);
		glm::vec3 a = glm::vec3(g[ik.thigh][3]);
		glm::vec3 b = glm::vec3(g[ik.shin][3]);
		glm::vec3 c = glm::vec3(g[ik.foot][3]);
		const glm::vec3 t    = ik.goalModelSpace;
		const glm::vec3 pole = ik.poleModelSpace;

		const float lab = glm::length(b - a);
		const float lcb = glm::length(c - b);
		if (lab < eps || lcb < eps) return;
		const float lat = glm::clamp(glm::length(t - a), eps, lab + lcb - eps);

		// 1) Bend the shin joint so the interior angle at B sets |A->C| to the reachable length.
		{
			const float cur = std::acos(glm::clamp(glm::dot(safeNorm(a - b), safeNorm(c - b)), -1.0f, 1.0f));
			const float des = std::acos(glm::clamp((lab * lab + lcb * lcb - lat * lat) / (2.0f * lab * lcb), -1.0f, 1.0f));
			glm::vec3 axis = glm::cross(a - b, c - b); // current limb-plane normal
			if (glm::length(axis) > 1e-6f)
				applyWorld(g, ik.shin, glm::angleAxis(des - cur, glm::normalize(axis)));
		}

		// 2) Swing the thigh so the foot points at the goal (foot distance is already correct).
		resolveGlobals(skel, pose, g);
		a = glm::vec3(g[ik.thigh][3]);
		c = glm::vec3(g[ik.foot][3]);
		{
			const glm::vec3 from = safeNorm(c - a), to = safeNorm(t - a);
			const float d = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
			if (d < 0.99999f) {
				glm::vec3 axis = glm::cross(from, to);
				axis = (glm::length(axis) > 1e-6f) ? glm::normalize(axis)
				                                   : safeNorm(glm::cross(from, glm::vec3(1, 0, 0)));
				applyWorld(g, ik.thigh, glm::angleAxis(std::acos(d), axis));
			}
		}

		// 3) Twist the limb about the A->goal axis so the elbow/knee faces the pole.
		resolveGlobals(skel, pose, g);
		a = glm::vec3(g[ik.thigh][3]);
		b = glm::vec3(g[ik.shin][3]);
		{
			const glm::vec3 nrm = safeNorm(t - a);
			if (glm::length(nrm) > 1e-6f) {
				glm::vec3 bproj = (b - a) - glm::dot(b - a, nrm) * nrm;     // elbow offset from the axis
				glm::vec3 pproj = (pole - a) - glm::dot(pole - a, nrm) * nrm; // pole offset from the axis
				if (glm::length(bproj) > 1e-5f && glm::length(pproj) > 1e-5f) {
					bproj = glm::normalize(bproj); pproj = glm::normalize(pproj);
					float ang = std::acos(glm::clamp(glm::dot(bproj, pproj), -1.0f, 1.0f));
					if (glm::dot(glm::cross(bproj, pproj), nrm) < 0.0f) ang = -ang;
					applyWorld(g, ik.thigh, glm::angleAxis(ang, nrm));
				}
			}
		}

		// 4) Blend toward the solved pose by weight.
		if (ik.weight < 1.0f) {
			pose[ik.thigh].rotation = glm::normalize(glm::slerp(origThigh, pose[ik.thigh].rotation, ik.weight));
			pose[ik.shin].rotation  = glm::normalize(glm::slerp(origShin,  pose[ik.shin].rotation,  ik.weight));
		}
	}

	void applyManualPose(const SkeletonData& skel, const Pose& editPose,
	                     const std::vector<IKSetup>& iks, Pose& outPose)
	{
		outPose = editPose;
		const int n = static_cast<int>(skel.jointCount());

		bool hasIK = false;
		for (const IKSetup& s : iks) if (s.valid()) { hasIK = true; break; }
		if (!hasIK || n == 0) return;

		// Goal/pole are joints — read their model-space positions from the base pose once, so all
		// setups solve against the same reference (matches the original per-frame palette pass).
		std::vector<glm::mat4> g;
		resolveGlobals(skel, outPose, g);
		for (const IKSetup& s : iks)
		{
			if (!s.valid()) continue;
			if (s.goalJoint >= n || s.poleJoint >= n) continue;
			TwoBoneIK ik;
			ik.thigh = s.thigh; ik.shin = s.shin; ik.foot = s.foot;
			ik.goalModelSpace = glm::vec3(g[s.goalJoint][3]);
			ik.poleModelSpace = glm::vec3(g[s.poleJoint][3]);
			ik.weight = s.weight;
			solveTwoBoneIK(skel, ik, outPose);
		}
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
