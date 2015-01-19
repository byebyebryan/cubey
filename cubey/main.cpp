
#define GLM_FORCE_PURE

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/ext.hpp"
#include "AntTweakBar.h"

#include <stdlib.h>
#include <stdio.h>

#include <iostream>
#include <thread>
#include <chrono>

#include "Engine.h"
#include "Shader.h"
#include "Vertex.h"

using namespace cubey;

int main(void)
{
	Engine::Init();

	TwInit(TW_OPENGL, NULL);
	EventChannel<Engine::WindowSizeEvent>::Add([](const Engine::WindowSizeEvent& e){TwWindowSize(e.width, e.height); });
	EventChannel<Engine::MouseButtonEvent>::Add([](const Engine::MouseButtonEvent& e){TwEventMouseButtonGLFW(e.button, e.action); });
	EventChannel<Engine::MousePosEvent>::Add([](const Engine::MousePosEvent& e){TwEventMousePosGLFW(e.xpos, e.ypos); });
	EventChannel<Engine::MouseWheelEvent>::Add([](const Engine::MouseWheelEvent& e){TwEventMouseWheelGLFW(e.yoffset); });
	EventChannel<Engine::KeyEvent>::Add([](const Engine::KeyEvent& e){TwEventKeyGLFW(e.key, e.action); });
	EventChannel<Engine::CharEvent>::Add([](const Engine::CharEvent& e){TwEventCharGLFW(e.codepoint, GLFW_PRESS); });

	EventChannel<Engine::UIRenderEvent>::Add([](const Engine::UIRenderEvent& e){TwDraw(); });
	EventChannel<Engine::TerminationEvent>::Add([](const Engine::TerminationEvent& e){TwTerminate(); });

	TwBar *bar = TwNewBar("TweakBar");
	TwWindowSize(800, 600);
	TwDefine(" GLOBAL help='AntTweakBar Test' ");

	TwAddVarRO(bar, "time", TW_TYPE_DOUBLE, &Engine::timer_, " label='Time' precision=2 help='Time (in seconds).' ");

	TwAddVarRO(bar, "fps", TW_TYPE_DOUBLE, &Engine::fps_, " label='FPS' precision=2 help='Frame Per Second' ");
	TwAddVarRO(bar, "delta time", TW_TYPE_DOUBLE, &Engine::delta_time_, " label='Delta Time' precision=4 help='Delta Time (in seconds).' ");
	
	TwAddVarRO(bar, "regulated fps", TW_TYPE_DOUBLE, &Engine::regulated_fps_, " label='Regulated FPS' precision=2 help='Frame Per Second' ");
	TwAddVarRO(bar, "regulated delta time", TW_TYPE_DOUBLE, &Engine::regulated_delta_time_, " label='Regulated Delta Time' precision=4 help='Delta Time (in seconds).' ");

	Engine::MainLoop();
	Engine::Terminate();

	exit(EXIT_SUCCESS);
}