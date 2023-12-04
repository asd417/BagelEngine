#include "keyboard_movement_controller.hpp"
#include <limits>

bool bagel::KeyboardMovementController::moveInPlaneXZ(GLFWwindow* window, float dt, BGLGameObject& gameObject, uint32_t transformIndex)
{
	bool updated = false;
	glm::vec3 rotate{ 0 };
	if (glfwGetKey(window, keys.lookRight) == GLFW_PRESS) rotate.y += 1.0f;
	if (glfwGetKey(window, keys.lookLeft) == GLFW_PRESS) rotate.y -= 1.0f;
	if (glfwGetKey(window, keys.lookUp) == GLFW_PRESS) rotate.x += 1.0f;
	if (glfwGetKey(window, keys.lookDown) == GLFW_PRESS) rotate.x -= 1.0f;
	// Good idea to avoid comparing float against 0
	if (glm::dot(rotate, rotate) > std::numeric_limits<float>::epsilon()) {
		gameObject.transform.rotation[transformIndex] += lookSpeed * dt * glm::normalize(rotate);
		updated = true;
	}

	gameObject.transform.rotation[transformIndex].x = glm::clamp(gameObject.transform.rotation[transformIndex].x, -1.5f, 1.5f);
	gameObject.transform.rotation[transformIndex].y = glm::mod(gameObject.transform.rotation[transformIndex].y, glm::two_pi<float>());

	float yaw = gameObject.transform.rotation[transformIndex].y;
	const glm::vec3 forwardDir{ sin(yaw),0.0f,cos(yaw) };
	const glm::vec3 rightDir{ forwardDir.z,0.0f,-forwardDir.x };
	const glm::vec3 upDir{ 0.0f,-1.0f,0.0f };

	glm::vec3 moveDir{ 0.0f };
	if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS) moveDir += forwardDir;
	if (glfwGetKey(window, keys.moveBack) == GLFW_PRESS) moveDir -= forwardDir;
	if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS) moveDir += rightDir;
	if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS) moveDir -= rightDir;
	if (glfwGetKey(window, keys.moveUp) == GLFW_PRESS) moveDir += upDir;
	if (glfwGetKey(window, keys.moveDown) == GLFW_PRESS) moveDir -= upDir;
	if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
		gameObject.transform.translation[transformIndex] += moveSpeed * dt * glm::normalize(moveDir);
		updated = true;
		//std::cout << gameObject.transform.translation[transformIndex].x << " " << gameObject.transform.translation[transformIndex].y << " " << gameObject.transform.translation[transformIndex].z << "\n";
	}
	return updated;
}
