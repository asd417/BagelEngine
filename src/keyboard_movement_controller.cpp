#include "keyboard_movement_controller.hpp"
#include <glm/gtc/constants.hpp>
#include <limits>
#include "ecs/components/transform.hpp"
#include "imgui.h"

bool bagel::KeyboardMovementController::moveInPlaneXZ(GLFWwindow* window, float dt, TransformComponent& tc, uint32_t transformIndex)
{
	ImGuiIO& io = ImGui::GetIO();
	bool updated = false;

	// Z toggles FPS mode (blocked when ImGui wants the keyboard)
	bool zDown = !io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
	if (zDown && !zKeyWasDown) {
		fpsMode = !fpsMode;
		if (fpsMode) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			double mx, my;
			glfwGetCursorPos(window, &mx, &my);
			lastMousePos = { static_cast<float>(mx), static_cast<float>(my) };
		} else {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}
	zKeyWasDown = zDown;

	// Mouse look — raw delta tracked via lastMousePos in FPS mode.
	// WantCaptureMouse is NOT checked here: in FPS mode the cursor is GLFW_CURSOR_DISABLED
	// so ImGui cannot actually receive clicks, but its virtual-position logic can still set
	// WantCaptureMouse=true, which would incorrectly block camera rotation.
	if (fpsMode) {
		double mx, my;
		glfwGetCursorPos(window, &mx, &my);
		glm::vec2 delta{ static_cast<float>(mx) - lastMousePos.x, static_cast<float>(my) - lastMousePos.y };
		lastMousePos = { static_cast<float>(mx), static_cast<float>(my) };

		if (glm::dot(delta, delta) > std::numeric_limits<float>::epsilon()) {
			glm::vec3 rot = tc.getRotation();
			rot.y -= glm::radians(delta.x) * mouseSensitivity;
			rot.x -= glm::radians(delta.y) * mouseSensitivity;
			rot.x = glm::clamp(rot.x, -1.5f, 1.5f);
			rot.y = glm::mod(rot.y, glm::two_pi<float>());
			tc.setRotation(rot);
			updated = true;
		}
	}

	if (io.WantCaptureKeyboard) return updated;

	// Arrow key look
	glm::vec3 rotate{ 0 };
	if (glfwGetKey(window, keys.lookRight) == GLFW_PRESS) rotate.y -= 1.0f;
	if (glfwGetKey(window, keys.lookLeft)  == GLFW_PRESS) rotate.y += 1.0f;
	if (glfwGetKey(window, keys.lookUp)    == GLFW_PRESS) rotate.x += 1.0f;
	if (glfwGetKey(window, keys.lookDown)  == GLFW_PRESS) rotate.x -= 1.0f;
	if (glm::dot(rotate, rotate) > std::numeric_limits<float>::epsilon()) {
		glm::vec3 rot = tc.getRotation() + lookSpeed * dt * glm::normalize(rotate);
		rot.x = glm::clamp(rot.x, -1.5f, 1.5f);
		rot.y = glm::mod(rot.y, glm::two_pi<float>());
		tc.setRotation(rot);
		updated = true;
	}

	// WASD movement
	float yaw   = tc.getRotation().y;
	float pitch = tc.getRotation().x;
	const glm::vec3 forwardDir{ cosf(pitch) * sinf(yaw), -sinf(pitch), cosf(pitch) * cosf(yaw) };
	const glm::vec3 rightDir{ cosf(yaw), 0.0f, -sinf(yaw) };
	const glm::vec3 upDir{ 0.0f, 1.0f, 0.0f };

	glm::vec3 moveDir{ 0.0f };
	if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS) moveDir -= forwardDir;
	if (glfwGetKey(window, keys.moveBack)    == GLFW_PRESS) moveDir += forwardDir;
	if (glfwGetKey(window, keys.moveRight)   == GLFW_PRESS) moveDir += rightDir;
	if (glfwGetKey(window, keys.moveLeft)    == GLFW_PRESS) moveDir -= rightDir;
	if (glfwGetKey(window, keys.moveUp)      == GLFW_PRESS) moveDir += upDir;
	if (glfwGetKey(window, keys.moveDown)    == GLFW_PRESS) moveDir -= upDir;
	if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
		tc.setTranslation(tc.getTranslation() + moveSpeed * dt * glm::normalize(moveDir));
		updated = true;
	}
	return updated;
}
