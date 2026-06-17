#include "pose_gizmo.hpp"

#include "bagel_ecs_components.hpp"          // TransformComponent, ModelComponent, AnimationComponent
#include "animation/bagel_animation.hpp"     // resolveGlobals

#include <GLFW/glfw3.h>
#include "imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace bagel {

	static glm::vec3 axisVec(int i) {
		return (i == 0) ? glm::vec3(1, 0, 0) : (i == 1) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
	}

	// Closest approach between ray (o,d) and infinite line through p along unit a.
	// Returns the line parameter s (signed world distance along a from p) and the two closest
	// points. False if near-parallel or the closest ray point is behind the camera.
	static bool closestOnAxis(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p,
	                          const glm::vec3& a, float& s, glm::vec3& axisPoint, glm::vec3& rayPoint)
	{
		const glm::vec3 e = p - o;
		const float ad = glm::dot(a, d);
		const float dd = glm::dot(d, d);
		const float ae = glm::dot(a, e);
		const float de = glm::dot(d, e);
		const float denom = ad * ad - dd;
		if (glm::abs(denom) < 1e-6f) return false;
		const float t = (ae * ad - de) / denom;
		s = t * ad - ae;
		axisPoint = p + s * a;
		rayPoint  = o + t * d;
		return t > 0.0f;
	}

	static bool rayPlane(const glm::vec3& o, const glm::vec3& d, const glm::vec3& c,
	                     const glm::vec3& n, glm::vec3& hit)
	{
		const float dn = glm::dot(d, n);
		if (glm::abs(dn) < 1e-6f) return false;
		const float t = glm::dot(c - o, n) / dn;
		if (t <= 0.0f) return false;
		hit = o + t * d;
		return true;
	}

	static bool raySphere(const glm::vec3& o, const glm::vec3& d, const glm::vec3& c, float r, float& tHit)
	{
		const glm::vec3 oc = o - c;
		const float a = glm::dot(d, d);
		const float b = 2.0f * glm::dot(oc, d);
		const float cc = glm::dot(oc, oc) - r * r;
		const float disc = b * b - 4.0f * a * cc;
		if (disc < 0.0f) return false;
		const float sq = std::sqrt(disc);
		float t = (-b - sq) / (2.0f * a);
		if (t < 0.0f) t = (-b + sq) / (2.0f * a);
		if (t < 0.0f) return false;
		tHit = t;
		return true;
	}

	// Screen-constant marker radius for a joint at world position p (so far joints stay pickable).
	static float markerRadius(const glm::vec3& camPos, const glm::vec3& p) {
		return glm::max(0.04f, glm::length(camPos - p) * 0.03f);
	}

	glm::vec3 PoseGizmo::axisDir(int i) const
	{
		if (!localSpace || selJoint < 0 || selJoint >= static_cast<int>(globals.size()))
			return axisVec(i);
		const glm::vec3 d = glm::vec3((entityModel * globals[selJoint])[i]);
		const float len = glm::length(d);
		return (len > 1e-6f) ? d / len : axisVec(i);
	}

	glm::mat4 PoseGizmo::handleBasis() const
	{
		if (!localSpace || selJoint < 0 || selJoint >= static_cast<int>(globals.size()))
			return glm::mat4{ 1.0f };
		const glm::mat4 jw = entityModel * globals[selJoint];
		const glm::vec3 x = glm::vec3(jw[0]), y = glm::vec3(jw[1]), z = glm::vec3(jw[2]);
		const float lx = glm::length(x), ly = glm::length(y), lz = glm::length(z);
		if (lx < 1e-6f || ly < 1e-6f || lz < 1e-6f) return glm::mat4{ 1.0f };
		glm::mat4 B{ 1.0f };
		B[0] = glm::vec4(x / lx, 0.0f);
		B[1] = glm::vec4(y / ly, 0.0f);
		B[2] = glm::vec4(z / lz, 0.0f);
		return B;
	}

	void PoseGizmo::refreshTarget()
	{
		target = entt::null; selJoint = -1; dragAxis = -1; hoverAxis = -1;
		auto view = registry.view<TransformComponent, ModelComponent, AnimationComponent>();
		for (auto [e, tc, mc, anim] : view.each()) {
			if (!mc.isSkinned) continue;
			target = e;
			anim.manualPose = true; // draws now read the dynamic (edited) palette region
			anim.poseDirty  = true;
			break;
		}
	}

	void PoseGizmo::setEditMode(bool on)
	{
		editMode = on;
		if (on) refreshTarget();
		else { hoverAxis = -1; dragAxis = -1; }
	}

	void PoseGizmo::makeRay(GLFWwindow* window, const BGLCamera& camera, float vpW, float vpH,
	                        glm::vec3& outOrigin, glm::vec3& outDir) const
	{
		double mx, my; glfwGetCursorPos(window, &mx, &my);
		const float ndcX = static_cast<float>(2.0 * mx / vpW - 1.0);
		const float ndcY = static_cast<float>(2.0 * my / vpH - 1.0); // Vulkan: y-down, top = -1
		const glm::mat4 invVP = glm::inverse(camera.getProjection() * camera.getView());
		glm::vec4 farP = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		farP /= farP.w;
		outOrigin = camera.getPosition();
		outDir    = glm::normalize(glm::vec3(farP) - outOrigin);
	}

	int PoseGizmo::pickJoint(const glm::vec3& o, const glm::vec3& d) const
	{
		int best = -1; float bestT = 1e30f;
		for (size_t j = 0; j < jointWorldPos.size(); ++j) {
			float t;
			const float rr = markerRadius(o, jointWorldPos[j]);
			if (raySphere(o, d, jointWorldPos[j], rr, t) && t < bestT) { bestT = t; best = static_cast<int>(j); }
		}
		return best;
	}

	int PoseGizmo::pickAxis(const glm::vec3& o, const glm::vec3& d, glm::vec3& outHit, float& outAxisCoord) const
	{
		if (selJoint < 0) return -1;
		const glm::vec3 c = selJointWorldPos;
		if (gmode == GizmoMode::Translate) {
			float bestDist = gizmoScale * 0.18f; int best = -1;
			for (int i = 0; i < 3; ++i) {
				const glm::vec3 a = axisDir(i);
				float s; glm::vec3 ap, rp;
				if (!closestOnAxis(o, d, c, a, s, ap, rp)) continue;
				if (s < 0.0f || s > gizmoScale) continue; // only along the positive arrow
				const float dist = glm::length(ap - rp);
				if (dist < bestDist) { bestDist = dist; best = i; outHit = ap; outAxisCoord = s; }
			}
			return best;
		}
		// Rotate: ray vs each axis plane, accept if the hit radius is near the ring radius.
		float bestErr = gizmoScale * 0.18f; int best = -1;
		for (int i = 0; i < 3; ++i) {
			const glm::vec3 n = axisDir(i);
			glm::vec3 hit;
			if (!rayPlane(o, d, c, n, hit)) continue;
			const float err = glm::abs(glm::length(hit - c) - gizmoScale);
			if (err < bestErr) { bestErr = err; best = i; outHit = hit; outAxisCoord = 0.0f; }
		}
		return best;
	}

	void PoseGizmo::applyDrag(const glm::vec3& o, const glm::vec3& d)
	{
		if (selJoint < 0 || dragAxis < 0) return;
		auto& anim = registry.get<AnimationComponent>(target);
		const int parent = anim.skeleton.parents[selJoint];
		const glm::mat4 parentWorld = (parent >= 0 && parent < static_cast<int>(globals.size()))
			? entityModel * globals[parent] : entityModel;
		const glm::mat3 parentRotInv = glm::inverse(glm::mat3(parentWorld));
		const glm::vec3 axis = dragStartAxis;   // FIXED world-space axis captured at mouse-down
		const glm::vec3 c = dragStartCenter;    // FIXED origin captured at mouse-down (see header)

		if (gmode == GizmoMode::Translate) {
			float s; glm::vec3 ap, rp;
			if (!closestOnAxis(o, d, c, axis, s, ap, rp)) return;
			const glm::vec3 worldDelta = (s - dragStartAxisCoord) * axis;
			anim.editPose[selJoint].translation = dragStartTrans + parentRotInv * worldDelta;
		} else {
			glm::vec3 hit;
			if (!rayPlane(o, d, c, axis, hit)) return;
			glm::vec3 v0 = dragStartHit - c;
			glm::vec3 v1 = hit - c;
			if (glm::length(v0) < 1e-5f || glm::length(v1) < 1e-5f) return;
			v0 = glm::normalize(v0); v1 = glm::normalize(v1);
			float ang = std::acos(glm::clamp(glm::dot(v0, v1), -1.0f, 1.0f));
			if (glm::dot(glm::cross(v0, v1), axis) < 0.0f) ang = -ang;
			const glm::vec3 localAxis = glm::normalize(parentRotInv * axis);
			anim.editPose[selJoint].rotation = glm::normalize(glm::angleAxis(ang, localAxis) * dragStartRot);
		}
		anim.poseDirty = true;
	}

	void PoseGizmo::update(GLFWwindow* window, const BGLCamera& camera, float vpW, float vpH)
	{
		ImGuiIO& io = ImGui::GetIO();

		// G toggles pose-edit mode (blocked while ImGui wants the keyboard / a text field).
		const bool gDown = !io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
		if (gDown && !keyGPrev) setEditMode(!editMode);
		keyGPrev = gDown;

		if (!editMode || target == entt::null) { hoverAxis = -1; dragAxis = -1; return; }

		// T/R switch manipulation mode (kept off W/E so they don't fight camera movement).
		const bool tDown = !io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS;
		const bool rDown = !io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
		if (tDown && !keyTPrev) gmode = GizmoMode::Translate;
		if (rDown && !keyRPrev) gmode = GizmoMode::Rotate;
		keyTPrev = tDown; keyRPrev = rDown;

		// L toggles the handle/drag space between world (global) and bone-local.
		const bool lDown = !io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
		if (lDown && !keyLPrev) localSpace = !localSpace;
		keyLPrev = lDown;

		if (!registry.valid(target) || !registry.all_of<TransformComponent, AnimationComponent>(target)) {
			refreshTarget();
			if (target == entt::null) return;
		}
		auto& tc   = registry.get<TransformComponent>(target);
		auto& anim = registry.get<AnimationComponent>(target);
		if (anim.jointCount == 0 || anim.skeleton.empty()) return;

		// Resolve joint world positions for this frame.
		entityModel = tc.mat4();
		resolveGlobals(anim.skeleton, anim.editPose, globals);
		const int n = static_cast<int>(anim.jointCount);
		jointWorldPos.resize(n);
		for (int j = 0; j < n; ++j) jointWorldPos[j] = glm::vec3(entityModel * globals[j][3]);
		parentsCache = anim.skeleton.parents; // expose hierarchy to the render system (bone lines)
		if (selJoint >= n) selJoint = -1;
		if (selJoint >= 0) selJointWorldPos = jointWorldPos[selJoint];

		const glm::vec3 camPos = camera.getPosition();
		const glm::vec3 center = (selJoint >= 0) ? selJointWorldPos : glm::vec3(entityModel[3]);
		gizmoScale = glm::max(0.05f, glm::length(camPos - center) * 0.12f);

		glm::vec3 ro, rd; makeRay(window, camera, vpW, vpH, ro, rd);
		const bool mouseLeft = !io.WantCaptureMouse && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

		if (mouseLeft && !mouseLeftPrev) {
			// Mouse down: grab an axis handle if one is under the cursor, otherwise select a joint.
			glm::vec3 hit; float axisCoord = 0.0f;
			const int axis = (selJoint >= 0) ? pickAxis(ro, rd, hit, axisCoord) : -1;
			if (axis >= 0) {
				dragAxis = axis;
				dragStartAxis = axisDir(axis); dragStartCenter = selJointWorldPos; dragStartHit = hit;
				dragStartAxisCoord = axisCoord;
				dragStartTrans = anim.editPose[selJoint].translation;
				dragStartRot   = anim.editPose[selJoint].rotation;
			} else {
				const int j = pickJoint(ro, rd);
				if (j >= 0) { selJoint = j; selJointWorldPos = jointWorldPos[j]; }
				dragAxis = -1;
			}
		} else if (mouseLeft && dragAxis >= 0) {
			applyDrag(ro, rd);
		} else if (!mouseLeft) {
			dragAxis = -1;
			glm::vec3 hit; float axisCoord = 0.0f;
			hoverAxis = (selJoint >= 0) ? pickAxis(ro, rd, hit, axisCoord) : -1;
		}
		mouseLeftPrev = mouseLeft;
	}

} // namespace bagel
