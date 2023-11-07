#pragma once


// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {
	class BGLCamera {
	public:
		void setOrthographicProjection(float left, float right, float top, float bottom, float near, float far);
		void setPerspectiveProjection(float fovy, float aspect, float near, float far);

		void setViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up = glm::vec3{ 0.0f,-1.0f,0.0f });
		void setViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up = glm::vec3{ 0.0f,-1.0f,0.0f });
		void setViewYXZ(glm::vec3 position, glm::vec3 rotation);

		const glm::mat4& getProjection() const { return projectionMatrix; }
		const glm::mat4& getView() const { return viewMatrix; }
		const glm::mat4& getInverseView() const { return inverseViewMatrix; }
		const glm::vec3 getPosition() const { return glm::vec3(inverseViewMatrix[3]); }
	private:
		void setViewInverseView(glm::vec3 position, glm::vec3 u, glm::vec3 v, glm::vec3 w);
		glm::mat4 projectionMatrix{ 1.f };
		glm::mat4 viewMatrix{ 1.0f }; // From world space to camera space
		glm::mat4 inverseViewMatrix{ 1.0f }; // From camera spacee to world space
	};
}