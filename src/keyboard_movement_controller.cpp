#include "keyboard_movement_controller.hpp"
#include <limits>
#include "imgui.h"

bool bagel::KeyboardMovementController::moveInPlaneXZ(GLFWwindow* window, float dt, BGLGameObject& gameObject, uint32_t transformIndex)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureKeyboard) return false;
	bool updated = false;
	glm::vec3 rotate{ 0 };
	if (glfwGetKey(window, keys.lookRight) == GLFW_PRESS) rotate.y -= 1.0f;
	if (glfwGetKey(window, keys.lookLeft) == GLFW_PRESS) rotate.y += 1.0f;
	if (glfwGetKey(window, keys.lookUp) == GLFW_PRESS) rotate.x += 1.0f;
	if (glfwGetKey(window, keys.lookDown) == GLFW_PRESS) rotate.x -= 1.0f;
	// Good idea to avoid comparing float against 0
	if (glm::dot(rotate, rotate) > std::numeric_limits<float>::epsilon()) {

		glm::vec3 newRot = gameObject.transform.getRotation() + lookSpeed * dt * glm::normalize(rotate);
		gameObject.transform.setRotation(newRot);
		updated = true;
	}
	gameObject.transform.setRotation({ glm::clamp(gameObject.transform.getRotation().x, -1.5f, 1.5f), glm::mod(gameObject.transform.getRotation().y, glm::two_pi<float>()), gameObject.transform.getRotation().z });

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
