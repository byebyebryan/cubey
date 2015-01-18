#pragma once

#include <iostream>
#include <functional>

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "AntTweakBar.h"

#include "Event.h"

namespace cubey {
	DECLARE_EVENT_TYPE0(SystemInitEvent);

	DECLARE_EVENT_TYPE2(WindowSizeEvent, int, int);
	DECLARE_EVENT_TYPE3(MouseButtonEvent, int, int, int);
	DECLARE_EVENT_TYPE2(MousePosEvent, double, double);
	DECLARE_EVENT_TYPE2(MouseWheelEvent, double, double);
	DECLARE_EVENT_TYPE4(KeyEvent, int, int, int, int);
	DECLARE_EVENT_TYPE1(CharEvent, unsigned int);

	class System {
	public:
		static System* Main() {
			static System instance;
			return &instance;
		}

		bool Init();

		static void ErrorHandler(int error, const char* description);
		static void WindowSizeHandler(GLFWwindow* window, int width, int height);
		static void MouseButtonHandler(GLFWwindow* window, int button, int action, int mods);
		static void MousePosHandler(GLFWwindow* window, double xpos, double ypos);
		static void MouseWheelHandler(GLFWwindow* window, double xoffset, double yoffset);
		static void KeyHandler(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void CharHandler(GLFWwindow* window, unsigned int codepoint);

		GLFWwindow* window_;
	private:
		System() {}
		
	};
}


