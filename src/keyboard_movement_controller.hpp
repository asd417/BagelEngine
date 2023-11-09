#pragma once

#include "bgl_gameobject.hpp"
#include "bagel_window.hpp"

namespace bagel {
	class KeyboardMovementController {
	public:
		struct KeyMappings {
			int moveLeft = GLFW_KEY_A;
			int moveRight = GLFW_KEY_D;
			int moveForward = GLFW_KEY_W;
			int moveBack = GLFW_KEY_S;
			int moveUp = GLFW_KEY_Q;
			int moveDown = GLFW_KEY_E;
			int lookLeft = GLFW_KEY_LEFT;
			int lookRight = GLFW_KEY_RIGHT;
			int lookUp = GLFW_KEY_UP;
			int lookDown = GLFW_KEY_DOWN;
		};

		bool moveInPlaneXZ(GLFWwindow* window, float dt, BGLGameObject& gameObject, uint32_t transformIndex);


		KeyMappings keys{};
		float moveSpeed{ 3.0f };
		float lookSpeed{ 1.5f };
	};
}