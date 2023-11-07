#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>


//https://www.youtube.com/watch?v=_riranMmtvI&ab_channel=BrendanGalea

namespace bagel {
	class BGLWindow {
	public:
		BGLWindow(int w, int h, std::string name);
		~BGLWindow();

		// Resource acquisition is initialization
		// Blocking a copy constructor means we wont accidentally delete both window and glfw window 
		// while also having another copy of window that has dangling pointer towards already deleted glfw window.
		BGLWindow(const BGLWindow&) = delete;
		BGLWindow& operator=(const BGLWindow&) = delete;

		bool shouldClose() { return glfwWindowShouldClose(window); }
		void createWindowSurface(VkInstance instance, VkSurfaceKHR* surface);
		VkExtent2D getExtent() { return { static_cast<uint32_t> (width), static_cast<uint32_t> (height) }; }

		void resetWindowResizedFlag() { frameBufferResized = false; }
		bool wasWindowResized() { return frameBufferResized; }

		GLFWwindow* getGLFWWindow() const { return window; }

	private:
		static void frameBufferResizedCallback(GLFWwindow* window, int width, int height);
		void initWindow();

		int width;
		int height;
		bool frameBufferResized = false;

		std::string windowName;
		GLFWwindow* window;
	};
}