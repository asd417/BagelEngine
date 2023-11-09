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
		gameObject.transforms[transformIndex].rotation += lookSpeed * dt * glm::normalize(rotate);
		updated = true;
	}

	gameObject.transforms[transformIndex].rotation.x = glm::clamp(gameObject.transforms[transformIndex].rotation.x, -1.5f, 1.5f);
	gameObject.transforms[transformIndex].rotation.y = glm::mod(gameObject.transforms[transformIndex].rotation.y, glm::two_pi<float>());

	float yaw = gameObject.transforms[transformIndex].rotation.y;
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
		gameObject.transforms[transformIndex].translation += moveSpeed * dt * glm::normalize(moveDir);
		updated = true;
	}
	return updated;
}
