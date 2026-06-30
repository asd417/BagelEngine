#pragma once
// -----------------------------------------------------------------------------
// In-viewport planet height painter. Mirrors PoseGizmo's shape (an edit-mode latch
// ticked once per frame from Application::run with the cursor ray), but instead of
// posing bones it raises/lowers the planet surface by writing into the paint
// cube-map (PlanetTerrain::paintBrush). The terrain marks the touched faces dirty;
// PlanetComponentSystem::update uploads them and rebuilds the mesh next frame.
//
// Header-only on purpose: the build globs sources at CMake-configure time, so a new
// .cpp wouldn't be picked up by an MSBuild-only rebuild. No Vulkan here — pure input
// + registry, like PoseGizmo.
// -----------------------------------------------------------------------------
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "entt.hpp"

#include "bagel_camera.hpp"
#include "components/planet.hpp"
#include "bagel_ecs_components.hpp" // TransformComponent

namespace bagel {

	class PlanetPaintController {
	public:
		// Programmatic / UI edit-mode control (B also toggles it; see update()).
		void setEditMode(bool on) { editMode = on; }
		bool editModeOn() const { return editMode; }

		// Brush parameters, driven by the Maps panel UI.
		float brushRadiusDeg = 6.0f;   // geodesic brush radius (degrees of arc)
		float strength       = 1.0f;   // world-units added per painted frame
		bool  lower          = false;  // false = raise, true = carve down
		int   targetLevel    = 7;      // subdivision forced under the brush (mesh detail)

		// Per-frame tick. vpW/vpH are the viewport pixel dimensions (for the mouse ray).
		void update(GLFWwindow* window, const BGLCamera& camera, float vpW, float vpH, entt::registry& registry)
		{
			// B toggles paint mode (edge-triggered), but not while typing in a text field.
			const bool bDown = glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS;
			if (bDown && !keyBPrev && !ImGui::GetIO().WantTextInput) setEditMode(!editMode);
			keyBPrev = bDown;

			if (!editMode) { mouseLeftPrev = false; return; }
			// Don't paint through ImGui windows (brush panel, console, ...).
			if (ImGui::GetIO().WantCaptureMouse) { mouseLeftPrev = false; return; }

			// Paint the first planet in the scene that has a live terrain.
			PlanetComponent* pc = nullptr;
			const TransformComponent* tc = nullptr;
			for (auto [e, t, p] : registry.view<TransformComponent, PlanetComponent>().each()) {
				if (!p.terrain) continue;
				tc = &t; pc = &p; break;
			}
			if (!pc) return;

			const bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
			if (!lmb) { mouseLeftPrev = false; return; }

			glm::vec3 o, d;
			screenRay(window, camera, vpW, vpH, o, d);
			const glm::vec3 center = tc->getWorldTranslation();
			const float r = pc->terrain->config().radius;
			float t;
			if (!raySphere(o, d, center, r, t)) { mouseLeftPrev = lmb; return; }

			const glm::vec3 dir = glm::normalize((o + d * t) - center);
			const float delta = (lower ? -1.0f : 1.0f) * strength;
			pc->terrain->paintBrush(dir, glm::radians(brushRadiusDeg), delta, targetLevel);
			mouseLeftPrev = lmb;
		}

	private:
		bool editMode = false, mouseLeftPrev = false, keyBPrev = false;

		// Cursor -> world ray (NDC -> inverse(proj*view) far point). Mirrors PoseGizmo::makeRay.
		static void screenRay(GLFWwindow* w, const BGLCamera& cam, float vpW, float vpH, glm::vec3& o, glm::vec3& d)
		{
			double mx, my; glfwGetCursorPos(w, &mx, &my);
			const float ndcX = static_cast<float>(2.0 * mx / vpW - 1.0);
			const float ndcY = static_cast<float>(2.0 * my / vpH - 1.0); // Vulkan: y-down
			const glm::mat4 invVP = glm::inverse(cam.getProjection() * cam.getView());
			glm::vec4 farP = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
			farP /= farP.w;
			o = cam.getPosition();
			d = glm::normalize(glm::vec3(farP) - o);
		}

		// Nearest ray-sphere hit (same math as PoseGizmo::raySphere).
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
	};

} // namespace bagel
