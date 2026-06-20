#include "pose_gizmo.hpp"

#include "bagel_ecs_components.hpp"          // TransformComponent, ModelComponent, AnimationComponent
#include "animation/bagel_animation.hpp"     // resolveGlobals

#include <GLFW/glfw3.h>
#include "imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace bagel {

	bool PoseGizmo::isSelected(int j) const {
		return std::find(selJoints.begin(), selJoints.end(), j) != selJoints.end();
	}

	// True if any ancestor of j (walking parents) is itself selected. Such a joint is skipped when
	// batching a transform: its selected ancestor already moves the whole subtree rigidly, so
	// transforming j again would double-apply the delta.
	bool PoseGizmo::ancestorSelected(int j) const {
		int p = (j >= 0 && j < static_cast<int>(parentsCache.size())) ? parentsCache[j] : -1;
		int guard = 0;
		while (p >= 0 && guard++ < 256) {
			if (isSelected(p)) return true;
			p = (p < static_cast<int>(parentsCache.size())) ? parentsCache[p] : -1;
		}
		return false;
	}

	void PoseGizmo::selectSingle(int j) {
		selJoints.assign(1, j);
		selJoint = j;
	}

	void PoseGizmo::toggleSelect(int j) {
		auto it = std::find(selJoints.begin(), selJoints.end(), j);
		if (it != selJoints.end()) {
			selJoints.erase(it);
			selJoint = selJoints.empty() ? -1 : selJoints.back();
		} else {
			selJoints.push_back(j);
			selJoint = j; // newly added becomes the active member
		}
	}

	void PoseGizmo::buildDragJoints() {
		dragJoints.clear();
		for (int j : selJoints) {
			if (ancestorSelected(j)) continue; // descendants follow their selected ancestor rigidly
			const int parent = (j < static_cast<int>(parentsCache.size())) ? parentsCache[j] : -1;
			const glm::mat4 parentWorld = (parent >= 0 && parent < static_cast<int>(globals.size()))
				? entityModel * globals[parent] : entityModel;
			DragJoint dj{};
			dj.joint         = j;
			dj.parentWorldInv = glm::inverse(parentWorld);
			dj.parentRot      = glm::quat_cast(glm::mat3(parentWorld));
			dj.startWorldPos  = jointWorldPos[j];
			dj.startWorldRot  = glm::quat_cast(glm::mat3(jointWorldMat[j]));
			dragJoints.push_back(dj);
		}
	}

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
		target = entt::null; selJoint = -1; selJoints.clear(); dragAxis = -1; hoverAxis = -1;
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
		if (selJoints.empty()) return -1;
		const glm::vec3 c = selCenter; // handles live at the selection centroid

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
		if (dragAxis < 0 || dragJoints.empty()) return;
		auto& anim = registry.get<AnimationComponent>(target);
		const glm::vec3 axis = dragStartAxis;   // FIXED world-space axis captured at mouse-down
		const glm::vec3 c = dragStartCenter;    // FIXED pivot (selection centroid) captured at mouse-down

		if (gmode == GizmoMode::Translate) {
			float s; glm::vec3 ap, rp;
			if (!closestOnAxis(o, d, c, axis, s, ap, rp)) return;
			const glm::vec3 worldDelta = (s - dragStartAxisCoord) * axis;
			// Shift every batched joint by the same world delta; orientation is untouched.
			for (const DragJoint& dj : dragJoints) {
				const glm::vec3 wp = dj.startWorldPos + worldDelta;
				anim.editPose[dj.joint].translation = glm::vec3(dj.parentWorldInv * glm::vec4(wp, 1.0f));
			}
		} else {
			glm::vec3 hit;
			if (!rayPlane(o, d, c, axis, hit)) return;
			glm::vec3 v0 = dragStartHit - c;
			glm::vec3 v1 = hit - c;
			if (glm::length(v0) < 1e-5f || glm::length(v1) < 1e-5f) return;
			v0 = glm::normalize(v0); v1 = glm::normalize(v1);
			float ang = std::acos(glm::clamp(glm::dot(v0, v1), -1.0f, 1.0f));
			if (glm::dot(glm::cross(v0, v1), axis) < 0.0f) ang = -ang;
			const glm::quat Qw = glm::angleAxis(ang, glm::normalize(axis)); // world-space rotation about the pivot
			// Orbit each batched joint around the pivot (position) and co-rotate its orientation.
			// For a single selection the pivot is the joint itself, so this reduces to spin-in-place.
			for (const DragJoint& dj : dragJoints) {
				const glm::vec3 wp = c + Qw * (dj.startWorldPos - c);
				const glm::quat wr = Qw * dj.startWorldRot;
				anim.editPose[dj.joint].translation = glm::vec3(dj.parentWorldInv * glm::vec4(wp, 1.0f));
				anim.editPose[dj.joint].rotation    = glm::normalize(glm::inverse(dj.parentRot) * wr);
			}
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

		// Resolve joint world positions for this frame from the IK-corrected pose (editPose + IK),
		// the same final pose the GPU palette bakes — so markers/handles sit on the bones' actual
		// posed positions, not the pre-IK authored ones.
		entityModel = tc.mat4();
		Pose displayPose;
		applyManualPose(anim.skeleton, anim.editPose, anim.ikSetups, displayPose);
		resolveGlobals(anim.skeleton, displayPose, globals);
		const int n = static_cast<int>(anim.jointCount);
		jointWorldPos.resize(n);
		jointWorldMat.resize(n);
		for (int j = 0; j < n; ++j) {
			jointWorldMat[j] = entityModel * globals[j];
			jointWorldPos[j] = glm::vec3(jointWorldMat[j][3]);
		}
		parentsCache = anim.skeleton.parents; // expose hierarchy to the render system (bone lines)

		// Drop any selections that fell out of range (e.g. target swap), then recompute the batch
		// pivot as the centroid of the selected joints' world positions.
		selJoints.erase(std::remove_if(selJoints.begin(), selJoints.end(),
			[&](int j) { return j < 0 || j >= n; }), selJoints.end());
		if (selJoint >= n || !isSelected(selJoint)) selJoint = selJoints.empty() ? -1 : selJoints.back();
		if (selJoint >= 0) selJointWorldPos = jointWorldPos[selJoint];
		if (selJoints.empty()) {
			selCenter = glm::vec3(entityModel[3]);
		} else {
			glm::vec3 sum{ 0.0f };
			for (int j : selJoints) sum += jointWorldPos[j];
			selCenter = sum / static_cast<float>(selJoints.size());
		}

		const glm::vec3 camPos = camera.getPosition();
		const glm::vec3 center = selJoints.empty() ? glm::vec3(entityModel[3]) : selCenter;
		gizmoScale = glm::max(0.05f, glm::length(camPos - center) * 0.12f);

		const bool shiftDown = !io.WantCaptureKeyboard &&
			(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS ||
			 glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

		glm::vec3 ro, rd; makeRay(window, camera, vpW, vpH, ro, rd);
		const bool mouseLeft = !io.WantCaptureMouse && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

		if (mouseLeft && !mouseLeftPrev) {
			// Mouse down: grab an axis handle if one is under the cursor, otherwise select a joint.
			// Shift+click toggles a joint in/out of the multi-selection; a plain click replaces it.
			glm::vec3 hit; float axisCoord = 0.0f;
			const int axis = !selJoints.empty() ? pickAxis(ro, rd, hit, axisCoord) : -1;
			if (axis >= 0) {
				dragAxis = axis;
				dragStartAxis = axisDir(axis); dragStartCenter = selCenter; dragStartHit = hit;
				dragStartAxisCoord = axisCoord;
				buildDragJoints(); // snapshot the top-level selected joints for the whole drag
			} else {
				const int j = pickJoint(ro, rd);
				if (j >= 0) { if (shiftDown) toggleSelect(j); else selectSingle(j); }
				else if (!shiftDown) selJoints.clear(), selJoint = -1; // plain click on empty clears
				dragAxis = -1;
			}
		} else if (mouseLeft && dragAxis >= 0) {
			applyDrag(ro, rd);
		} else if (!mouseLeft) {
			dragAxis = -1;
			glm::vec3 hit; float axisCoord = 0.0f;
			hoverAxis = !selJoints.empty() ? pickAxis(ro, rd, hit, axisCoord) : -1;
		}
		mouseLeftPrev = mouseLeft;

		// Cache the selected bone's name for the overlay/UI.
		selJointName = (selJoint >= 0 && selJoint < static_cast<int>(anim.skeleton.names.size()))
			? anim.skeleton.names[selJoint] : std::string{};
	}

} // namespace bagel
