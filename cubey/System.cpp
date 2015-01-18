#include "System.h"


bool cubey::System::Init() {
	glfwSetErrorCallback((GLFWerrorfun)ErrorHandler);
	if (!glfwInit()) {
		exit(EXIT_FAILURE);
	}
		
	window_ = glfwCreateWindow(640, 480, "Simple example", NULL, NULL);
	if (!window_) {
		glfwTerminate();
		exit(EXIT_FAILURE);
	}
	glfwMakeContextCurrent(window_);

	glfwSetWindowSizeCallback(window_, (GLFWwindowsizefun)WindowSizeHandler);
	glfwSetMouseButtonCallback(window_, (GLFWmousebuttonfun)MouseButtonHandler);
	glfwSetCursorPosCallback(window_, (GLFWcursorposfun)MousePosHandler);
	glfwSetScrollCallback(window_, (GLFWscrollfun)MouseWheelHandler);
	glfwSetKeyCallback(window_, (GLFWkeyfun)KeyHandler);
	glfwSetCharCallback(window_, (GLFWcharfun)CharHandler);

	glewInit();

	EventChannel<SystemInitEvent>::Broadcast(SystemInitEvent());

	return true;
}

void cubey::System::ErrorHandler(int error, const char* description) {
	std::cerr << description << std::endl;
}

void cubey::System::WindowSizeHandler(GLFWwindow* window, int width, int height) {
	glfwSetWindowSize(window, width, height);
	EventChannel<WindowSizeEvent>::Broadcast(WindowSizeEvent{ width, height });
}

void cubey::System::MouseButtonHandler(GLFWwindow* window, int button, int action, int mods) {
	EventChannel<MouseButtonEvent>::Broadcast(MouseButtonEvent{ button, action, mods });
}

void cubey::System::MousePosHandler(GLFWwindow* window, double xpos, double ypos) {
	EventChannel<MousePosEvent>::Broadcast(MousePosEvent{ xpos, ypos });
}

void cubey::System::MouseWheelHandler(GLFWwindow* window, double xoffset, double yoffset) {
	EventChannel<MouseWheelEvent>::Broadcast(MouseWheelEvent{ xoffset, yoffset });
}

void cubey::System::KeyHandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
	EventChannel<KeyEvent>::Broadcast(KeyEvent{ key, scancode, action, mods });
}

void cubey::System::CharHandler(GLFWwindow* window, unsigned int codepoint) {
	EventChannel<CharEvent>::Broadcast(CharEvent{ codepoint });
}
