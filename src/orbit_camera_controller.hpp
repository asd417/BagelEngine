#pragma once
// -----------------------------------------------------------------------------
// Blender-style orbit ("turntable") camera.
//
// Holds the orbit state — a world-space pivot plus the spherical offset the eye sits at
// (distance/yaw/pitch) — and every frame writes the resulting pose back into the viewer
// entity's TransformComponent. Nothing downstream changes: run() still feeds that transform
// to BGLCamera::setViewYXZ, so the view/projection, shadow cascades and planet LOD keep
// reading the camera exactly as they do in free-fly mode.
//
// Input (matches Blender's viewport defaults):
//   MMB drag          orbit / tumble around the pivot
//   Shift + MMB drag  pan (moves the pivot; screen-space exact — the scene tracks the cursor)
//   Ctrl  + MMB drag  dolly
//   Mouse wheel       dolly in/out (Ctrl = fine step)
//
// ---- basis convention (do not "simplify" the trig below) ---------------------
// The engine's camera basis comes from BGLCamera::setViewYXZ with roll fixed at 0:
//     u = ( cos y,           0,      -sin y        )   view X  -> screen RIGHT
//     v = ( sin y * sin p,   cos p,   cos y * sin p)   view Y  -> screen UP
//     w = ( cos p * sin y,  -sin p,   cos p * cos y)   view Z  -> BACKWARD
// where y = rotation.y (yaw) and p = rotation.x (pitch).
//
// w points AWAY from what the camera sees: the projection matrix is premultiplied by
// diag(1,-1,-1) (BGLCamera's xSpaceTransformMatrix), so clip.w = -z_view and only points
// with dot(w, p - eye) < 0 are in front. run() relies on the same fact when it computes
// camFwd = -inverseView[2]. That is why the eye sits at `pivot + w * distance`.
// -----------------------------------------------------------------------------

#include "ecs/components/transform.hpp"
#include "engine/bagel_engine_config.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct GLFWwindow;

namespace bagel
{
	class OrbitCameraController
	{
	public:
		// Per-frame tick. Reads mouse state, updates the orbit state, then writes the pose into
		// `tc` (the viewer entity's transform). `fovY` is the VERTICAL fov in radians and `vpH`
		// the viewport height in pixels — together they make the pan exactly screen-space
		// accurate, so a dragged point stays under the cursor at any distance.
		void update(GLFWwindow *window, TransformComponent &tc, float fovY, float vpH);

		// (Re)derive pivot/yaw/pitch from the viewer's current pose, keeping the current
		// distance: the pivot lands `distance` units in front of the eye. Call this whenever
		// something else moved the camera — entering orbit mode, or a scene's spawn teleport.
		void syncFromTransform(const TransformComponent &tc);

		// Frame a world-space bounding sphere: pivot on it and back off far enough that it fits
		// the narrower of the two fov axes (with a small margin).
		void focusOn(const glm::vec3 &center, float radius, float fovY, float aspect);

		// Write the current orbit state into `tc` (also done at the end of update()).
		void applyToTransform(TransformComponent &tc) const;

		// Basis vectors for the current yaw/pitch — see the header comment.
		glm::vec3 backward() const;    // w: pivot -> eye
		glm::vec3 forward() const { return -backward(); } // what the camera looks along
		glm::vec3 screenRight() const; // u
		glm::vec3 screenUp() const;    // v

		// ---- tunables (Settings panel) ----
		float orbitSensitivity = cfg::kOrbitSensitivity; // radians per pixel of drag
		float panSensitivity = cfg::kOrbitPanSensitivity; // multiplier on the exact pan
		float zoomStep = cfg::kOrbitZoomStep;             // distance factor per wheel notch
		float minDistance = cfg::kOrbitMinDistance;
		float maxDistance = cfg::kOrbitMaxDistance;

		// ---- state (public: the Settings panel shows/edits it directly) ----
		glm::vec3 pivot{0.0f};
		float distance = cfg::kOrbitDistance;
		float yaw = 0.0f;   // rotation.y
		float pitch = 0.0f; // rotation.x

		bool isDragging() const { return dragging; }

	private:
		void clampState();

		bool dragging = false;
		glm::vec2 lastMouse{0.0f, 0.0f};
	};
}
