#include "bagel_window.hpp"

#include <stdexcept>

namespace bagel {
	BGLWindow::BGLWindow(int w, int h, std::string name) : width{ w },height{h}, windowName{name}
	{
		initWindow();
	}

	BGLWindow::~BGLWindow()
	{
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	void BGLWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR* surface)
	{
		if (glfwCreateWindowSurface(instance, window, nullptr, surface) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface");
		}
	}

	void BGLWindow::frameBufferResizedCallback(GLFWwindow* window, int width, int height)
	{
		auto bglWindow = reinterpret_cast<BGLWindow*>(glfwGetWindowUserPointer(window));
		bglWindow->frameBufferResized = true;
		bglWindow->width = width;
		bglWindow->height = height;
	}

	void BGLWindow::initWindow()
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		window = glfwCreateWindow(width, height, windowName.c_str(), nullptr, nullptr);

		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, frameBufferResizedCallback);
	}
}