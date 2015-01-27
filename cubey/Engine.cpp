#include "Engine.h"

#include <iostream>
#include <thread>
#include <chrono>
#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "Time.h"

#include "Camera.h"
#include "UI.h"
#include "Input.h"

namespace cubey {
	const std::string kDefaultWindowTitle = "cubey!";
	const int kDefaultWindowWidth = 800;
	const int kDefaultWindowHeight = 600;
	
	GLFWwindow* Engine::window_ = nullptr;

	void Engine::Init() {
		glfwSetErrorCallback((GLFWerrorfun)ErrorHandler);
		if (!glfwInit()) {
			exit(EXIT_FAILURE);
		}

		window_ = glfwCreateWindow(kDefaultWindowWidth, kDefaultWindowHeight, kDefaultWindowTitle.c_str(), NULL, NULL);
		if (!window_) {
			glfwTerminate();
			exit(EXIT_FAILURE);
		}

		glfwMakeContextCurrent(window_);

		glfwSetWindowSizeCallback(window_, (GLFWwindowsizefun)WindowSizeHandler);
		glfwSetFramebufferSizeCallback(window_, (GLFWframebuffersizefun)FrameBufferSizeHandler);
		glfwSetMouseButtonCallback(window_, (GLFWmousebuttonfun)MouseButtonHandler);
		glfwSetCursorPosCallback(window_, (GLFWcursorposfun)MousePosHandler);
		glfwSetScrollCallback(window_, (GLFWscrollfun)MouseWheelHandler);
		glfwSetKeyCallback(window_, (GLFWkeyfun)KeyHandler);
		glfwSetCharCallback(window_, (GLFWcharfun)CharHandler);

		glewInit();

		Input::Main();
		Camera::Main();
		UI::Main();
	}

	void Engine::MainLoop() {
		EventChannel<InitEvent>::Broadcast(InitEvent());

		Time::time_since_start_ = glfwGetTime();

		EventChannel<StartUpEvent>::Broadcast(StartUpEvent{});

		while (!glfwWindowShouldClose(window_)) {
			Time::delta_time_ = Time::frame_time_;
			Time::raw_fps_ = Time::regulated_fps_ = 1.0 / Time::frame_time_;

			double delta_time_deficit = Time::target_delta_time_ - Time::frame_time_;
			if (delta_time_deficit > 0.0) {
				std::this_thread::sleep_for(std::chrono::milliseconds((int)(delta_time_deficit * 1000.0)));
				Time::delta_time_ = glfwGetTime() - Time::time_since_start_;
				Time::regulated_fps_ = 1.0 / Time::delta_time_;
			}

			Time::time_since_start_ = glfwGetTime();
			ScopeTimer frame_timer(&Time::frame_time_);
			Update(Time::delta_time_);
			Render();
		}

		Terminate();
	}

	void Engine::Update(float delta_time) {
		ScopeTimer timer(&Time::logic_time_);
		EventChannel<EarlyUpdateEvent>::Broadcast(EarlyUpdateEvent{ delta_time });
		EventChannel<UpdateEvent>::Broadcast(UpdateEvent{ delta_time });
		EventChannel<LateUpdateEvent>::Broadcast(LateUpdateEvent{ delta_time });
	}

	void Engine::Render() {
		ScopeTimer timer(&Time::render_time_);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		EventChannel<RenderEvent>::Broadcast(RenderEvent{});
		EventChannel<UIRenderEvent>::Broadcast(UIRenderEvent{});
		glfwSwapBuffers(window_);
		glfwPollEvents();
	}

	void Engine::Terminate() {
		EventChannel<TerminateEvent>::Broadcast(TerminateEvent{});
		glfwDestroyWindow(window_);
		glfwTerminate();
	}

	void Engine::ErrorHandler(int error, const char* description) {
		std::cerr << description << std::endl;
	}

	void Engine::WindowSizeHandler(GLFWwindow* window, int width, int height) {
		glfwSetWindowSize(window, width, height);
		EventChannel<WindowSizeEvent>::Broadcast(WindowSizeEvent{ width, height });
	}

	void Engine::FrameBufferSizeHandler(GLFWwindow* window, int width, int height) {
		glViewport(0, 0, width, height);
		EventChannel<FrameBufferSizeEvent>::Broadcast(FrameBufferSizeEvent{ width, height });
	}

	void Engine::MouseButtonHandler(GLFWwindow* window, int button, int action, int mods) {
		EventChannel<MouseButtonEvent>::Broadcast(MouseButtonEvent{ button, action, mods });
	}

	void Engine::MousePosHandler(GLFWwindow* window, double xpos, double ypos) {
		EventChannel<MousePosEvent>::Broadcast(MousePosEvent{ xpos, ypos });
	}

	void Engine::MouseWheelHandler(GLFWwindow* window, double xoffset, double yoffset) {
		EventChannel<MouseWheelEvent>::Broadcast(MouseWheelEvent{ xoffset, yoffset });
	}

	void Engine::KeyHandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
		EventChannel<KeyEvent>::Broadcast(KeyEvent{ key, scancode, action, mods });
	}

	void Engine::CharHandler(GLFWwindow* window, unsigned int codepoint) {
		EventChannel<CharEvent>::Broadcast(CharEvent{ codepoint });
	}

}

