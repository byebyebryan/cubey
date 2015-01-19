#include "Engine.h"

#include <iostream>
#include <thread>
#include <chrono>

const std::string kDefaultWindowTitle = "cubey!";
const int kDefaultWindowWidth = 800;
const int kDefaultWindowHeight = 600;
const int kFPSLimit = 60;

GLFWwindow* cubey::Engine::window_ = nullptr;
double cubey::Engine::timer_ = 0.0;
double cubey::Engine::fps_ = 0.0;
double cubey::Engine::regulated_fps_ = 0.0;
double cubey::Engine::delta_time_ = 0.0;
double cubey::Engine::regulated_delta_time_ = 0.0;

void cubey::Engine::Init() {
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

	EventChannel<InitEvent>::Broadcast(InitEvent{});
}

void cubey::Engine::MainLoop() {
	timer_ = glfwGetTime();
	double target_delta_time = 1.0 / kFPSLimit;
	while (!glfwWindowShouldClose(window_)) {
		double current_time = glfwGetTime();
		regulated_delta_time_ = delta_time_ = current_time - timer_;
		regulated_fps_ = fps_ = 1.0 / delta_time_;

		double delta_time_deficit = target_delta_time - delta_time_;
		if (delta_time_deficit > 0.0) {
			std::this_thread::sleep_for(std::chrono::milliseconds((int)(delta_time_deficit * 1000.0)));
			current_time = glfwGetTime();
			regulated_delta_time_ = current_time - timer_;
			regulated_fps_ = 1.0 / regulated_delta_time_;
		}

		timer_ = current_time;
		Update(regulated_delta_time_);
	}
}

void cubey::Engine::Update(float delta_time) {
	EventChannel<EarlyUpdateEvent>::Broadcast(EarlyUpdateEvent{ delta_time });
	EventChannel<UpdateEvent>::Broadcast(UpdateEvent{ delta_time });
	EventChannel<LateUpdateEvent>::Broadcast(LateUpdateEvent{ delta_time });

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	EventChannel<RenderEvent>::Broadcast(RenderEvent{});
	EventChannel<UIRenderEvent>::Broadcast(UIRenderEvent{});
	glfwSwapBuffers(window_);
	glfwPollEvents();
}

void cubey::Engine::Terminate() {
	EventChannel<TerminationEvent>::Broadcast(TerminationEvent{});
	glfwDestroyWindow(window_);
	glfwTerminate();
}

void cubey::Engine::ErrorHandler(int error, const char* description) {
	std::cerr << description << std::endl;
}

void cubey::Engine::WindowSizeHandler(GLFWwindow* window, int width, int height) {
	glfwSetWindowSize(window, width, height);
	EventChannel<WindowSizeEvent>::Broadcast(WindowSizeEvent{ width, height });
}

void cubey::Engine::FrameBufferSizeHandler(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
	EventChannel<FrameBufferSizeEvent>::Broadcast(FrameBufferSizeEvent{ width, height });
}

void cubey::Engine::MouseButtonHandler(GLFWwindow* window, int button, int action, int mods) {
	EventChannel<MouseButtonEvent>::Broadcast(MouseButtonEvent{ button, action, mods });
}

void cubey::Engine::MousePosHandler(GLFWwindow* window, double xpos, double ypos) {
	EventChannel<MousePosEvent>::Broadcast(MousePosEvent{ xpos, ypos });
}

void cubey::Engine::MouseWheelHandler(GLFWwindow* window, double xoffset, double yoffset) {
	EventChannel<MouseWheelEvent>::Broadcast(MouseWheelEvent{ xoffset, yoffset });
}

void cubey::Engine::KeyHandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
	EventChannel<KeyEvent>::Broadcast(KeyEvent{ key, scancode, action, mods });
}

void cubey::Engine::CharHandler(GLFWwindow* window, unsigned int codepoint) {
	EventChannel<CharEvent>::Broadcast(CharEvent{ codepoint });
}

