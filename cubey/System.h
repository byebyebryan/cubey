#pragma once

#include <iostream>
#include <functional>

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "AntTweakBar.h"

#include "Event.h"

namespace cubey {
	struct SystemInitEvent {};

	struct WindowSizeEvent {
		int width;
		int height;
	};

	struct MouseButtonEvent {
		int button;
		int action;
		int mods;
	};

	struct MousePosEvent {
		double xpos;
		double ypos;
	};

	struct MouseWheelEvent {
		double xoffset;
		double yoffset;
	};

	struct KeyEvent {
		int key;
		int scancode;
		int action;
		int yoffset;
	};

	struct CharEvent {
		unsigned int codepoint;
	};

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


