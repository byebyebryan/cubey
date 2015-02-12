#pragma once

#include "Event.h"
#include "TimeManager.h"

struct GLFWwindow;

namespace cubey {

	class Engine {
	public:
		struct SystemInitEvent {
		};

		struct InitEvent {
		};

		struct StartUpEvent {
		};
		struct TerminateEvent {
		};

		struct EarlyUpdateEvent {
			float deltatime;
		};
		struct UpdateEvent {
			float deltatime;
		};
		struct LateUpdateEvent {
			float deltatime;
		};
		struct RenderEvent {
		};
		struct UIRenderEvent {
		};
		
		struct WindowSizeEvent {
			int width;
			int height;
		};
		struct FrameBufferSizeEvent {
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
			int mods;
		};
		struct CharEvent {
			unsigned int codepoint;
		};
	
		static void Init();
		static void MainLoop();
		static void Update(float delta_time);
		static void Render();
		static void Terminate();

		static void ErrorHandler(int error, const char* description);
		static void WindowSizeHandler(GLFWwindow* window, int width, int height);
		static void FrameBufferSizeHandler(GLFWwindow* window, int width, int height);
		static void MouseButtonHandler(GLFWwindow* window, int button, int action, int mods);
		static void MousePosHandler(GLFWwindow* window, double xpos, double ypos);
		static void MouseWheelHandler(GLFWwindow* window, double xoffset, double yoffset);
		static void KeyHandler(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void CharHandler(GLFWwindow* window, unsigned int codepoint);

		static GLFWwindow* window_;
	};
}


