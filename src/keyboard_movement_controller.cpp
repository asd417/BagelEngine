#include "keyboard_movement_controller.hpp"
#include <limits>
#include "imgui.h"

bool bagel::KeyboardMovementController::moveInPlaneXZ(GLFWwindow* window, float dt, BGLGameObject& gameObject, uint32_t transformIndex)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureKeyboard) return false;
	bool updated = false;

	// Mouse look — hold RMB to capture cursor and rotate camera
	bool rmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	if (rmbDown && !io.WantCaptureMouse) {
		double mx, my;
		glfwGetCursorPos(window, &mx, &my);
		glm::vec2 mousePos{ static_cast<float>(mx), static_cast<float>(my) };

		if (!mouseCaptured) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			lastMousePos = mousePos;
			mouseCaptured = true;
		}

		glm::vec2 delta = mousePos - lastMousePos;
		lastMousePos = mousePos;

		if (glm::dot(delta, delta) > std::numeric_limits<float>::epsilon()) {
			glm::vec3 rot = gameObject.transform.getRotation();
			rot.y -= glm::radians(delta.x) * mouseSensitivity;
			rot.x -= glm::radians(delta.y) * mouseSensitivity;
			rot.x = glm::clamp(rot.x, -1.5f, 1.5f);
			rot.y = glm::mod(rot.y, glm::two_pi<float>());
			gameObject.transform.setRotation(rot);
			updated = true;
		}
	} else if (mouseCaptured) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		mouseCaptured = false;
	}

	glm::vec3 rotate{ 0 };
	if (glfwGetKey(window, keys.lookRight) == GLFW_PRESS) rotate.y -= 1.0f;
	if (glfwGetKey(window, keys.lookLeft) == GLFW_PRESS) rotate.y += 1.0f;
	if (glfwGetKey(window, keys.lookUp) == GLFW_PRESS) rotate.x += 1.0f;
	if (glfwGetKey(window, keys.lookDown) == GLFW_PRESS) rotate.x -= 1.0f;
	if (glm::dot(rotate, rotate) > std::numeric_limits<float>::epsilon()) {
		glm::vec3 newRot = gameObject.transform.getRotation() + lookSpeed * dt * glm::normalize(rotate);
		gameObject.transform.setRotation(newRot);
		updated = true;
		gameObject.transform.setRotation({ glm::clamp(gameObject.transform.getRotation().x, -1.5f, 1.5f), glm::mod(gameObject.transform.getRotation().y, glm::two_pi<float>()), gameObject.transform.getRotation().z });
	}

	float yaw = gameObject.transform.getRotation().y;
	const glm::vec3 forwardDir{ sin(yaw),0.0f,cos(yaw) };
	const glm::vec3 rightDir{ forwardDir.z,0.0f,-forwardDir.x };
	const glm::vec3 upDir{ 0.0f,1.0f,0.0f };

	glm::vec3 moveDir{ 0.0f };
	if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS) moveDir -= forwardDir;
	if (glfwGetKey(window, keys.moveBack) == GLFW_PRESS) moveDir += forwardDir;
	if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS) moveDir += rightDir;
	if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS) moveDir -= rightDir;
	if (glfwGetKey(window, keys.moveUp) == GLFW_PRESS) moveDir += upDir;
	if (glfwGetKey(window, keys.moveDown) == GLFW_PRESS) moveDir -= upDir;
	if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
		gameObject.transform.setTranslation(gameObject.transform.getTranslation() + moveSpeed * dt * glm::normalize(moveDir));
		updated = true;
		//std::cout << gameObject.transform.translation[transformIndex].x << " " << gameObject.transform.translation[transformIndex].y << " " << gameObject.transform.translation[transformIndex].z << "\n";
	}
	return updated;
}
