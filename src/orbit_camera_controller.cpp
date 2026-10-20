#include "orbit_camera_controller.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cmath>
#include <glm/gtc/constants.hpp>

#include "imgui.h"

namespace bagel
{
	namespace
	{
		// Pitch never reaches straight up/down: at |pitch| = pi/2 the yaw axis and the view
		// direction line up and the basis degenerates (the view snaps/rolls).
		constexpr float kPitchLimit = glm::half_pi<float>() - 0.01f;

		bool keyDown(GLFWwindow *w, int a, int b)
		{
			return glfwGetKey(w, a) == GLFW_PRESS || glfwGetKey(w, b) == GLFW_PRESS;
		}
	}

	glm::vec3 OrbitCameraController::backward() const
	{
		const float cp = cosf(pitch), sp = sinf(pitch);
		const float cy = cosf(yaw), sy = sinf(yaw);
		return {cp * sy, -sp, cp * cy};
	}

	glm::vec3 OrbitCameraController::screenRight() const
	{
		return {cosf(yaw), 0.0f, -sinf(yaw)};
	}

	glm::vec3 OrbitCameraController::screenUp() const
	{
		const float cp = cosf(pitch), sp = sinf(pitch);
		return {sinf(yaw) * sp, cp, cosf(yaw) * sp};
	}

	void OrbitCameraController::clampState()
	{
		pitch = glm::clamp(pitch, -kPitchLimit, kPitchLimit);
		yaw = glm::mod(yaw, glm::two_pi<float>());
		distance = glm::clamp(distance, minDistance, maxDistance);
	}

	void OrbitCameraController::applyToTransform(TransformComponent &tc) const
	{
		tc.setRotation({pitch, yaw, 0.0f});
		tc.setTranslation(pivot + backward() * distance);
	}

	void OrbitCameraController::syncFromTransform(const TransformComponent &tc)
	{
		const glm::vec3 rot = tc.getRotation();
		pitch = rot.x;
		yaw = rot.y;
		clampState();
		// forward() is where the camera is aiming, so this puts the pivot dead centre of the
		// current view at the current distance — the view does not jump when orbit mode starts.
		pivot = tc.getTranslation() + forward() * distance;
	}

	void OrbitCameraController::focusOn(const glm::vec3 &center, float radius, float fovY, float aspect)
	{
		pivot = center;
		// Fit the sphere to whichever fov axis is tighter (a tall/narrow window is limited
		// horizontally). 1.1 leaves a little air around the subject.
		const float halfY = glm::max(fovY * 0.5f, 0.01f);
		const float halfX = atanf(tanf(halfY) * glm::max(aspect, 0.01f));
		const float half = glm::min(halfX, halfY);
		distance = glm::max(radius, 0.01f) / glm::max(sinf(half), 0.01f) * 1.1f;
		clampState();
	}

	void OrbitCameraController::update(GLFWwindow *window, TransformComponent &tc, float fovY, float vpH)
	{
		ImGuiIO &io = ImGui::GetIO();

		double mx = 0.0, my = 0.0;
		glfwGetCursorPos(window, &mx, &my);
		const glm::vec2 mouse{static_cast<float>(mx), static_cast<float>(my)};

		const bool shift = keyDown(window, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT);
		const bool ctrl = keyDown(window, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL);

		// A drag may only START outside ImGui, but once started it keeps going even if the
		// cursor crosses a panel — otherwise a tumble dies the moment it passes the console.
		const bool mmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
		if (mmb && !dragging && !io.WantCaptureMouse)
		{
			dragging = true;
			lastMouse = mouse;
		}
		if (!mmb)
			dragging = false;

		if (dragging)
		{
			const glm::vec2 d = mouse - lastMouse;
			lastMouse = mouse;
			if (shift)
			{
				// World units per pixel at the pivot's depth. With this the point under the
				// cursor stays under the cursor, near or far.
				const float worldPerPx = 2.0f * distance * tanf(fovY * 0.5f) / glm::max(vpH, 1.0f);
				// Drag right -> the scene follows the cursor right -> the eye moves LEFT (-u).
				// Drag down (screen y grows downward) -> the eye moves UP (+v).
				pivot += (screenUp() * d.y - screenRight() * d.x) * worldPerPx * panSensitivity;
			}
			else if (ctrl)
			{
				distance *= expf(d.y * 0.005f); // drag down = pull back
			}
			else
			{
				// Turntable: the scene tracks the cursor, so the EYE swings the other way.
				// dw/dyaw = cos(pitch) * u and dw/dpitch = -v, hence both signs negative.
				yaw -= d.x * orbitSensitivity;
				pitch -= d.y * orbitSensitivity;
			}
		}

		if (io.MouseWheel != 0.0f && !io.WantCaptureMouse)
		{
			// Geometric, not linear: each notch is a fixed fraction of the current distance, so
			// zooming stays usable whether the pivot is 0.1 or 500 units away.
			const float step = ctrl ? 1.0f + (zoomStep - 1.0f) * 0.25f : zoomStep;
			distance *= powf(glm::max(step, 1.001f), -io.MouseWheel);
		}

		clampState();
		applyToTransform(tc);
	}
}
