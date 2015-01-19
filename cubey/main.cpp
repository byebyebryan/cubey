
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
#include "Camera.h"

using namespace cubey;

GLuint vao = 0;
ShaderProgram* prog = nullptr;

void TestRender(const Engine::RenderEvent& e) {
	
	//m_mat = glm::translate(m_mat, glm::vec3(0.01f, 0.01f, 0.0f));

	glm::mat4 p_mat = Camera::Main()->GetProjectionMat();
	glm::mat4 v_mat = Camera::Main()->GetViewMat();
	glm::mat4 mvp_mat = p_mat * v_mat;

	prog->Activate();
	prog->SetUniform("u_mvp_mat", mvp_mat);

	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

int main(void) {
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

	Camera::Main()->Init();

	TwBar *bar = TwNewBar("TweakBar");
	TwWindowSize(800, 600);
	TwDefine(" GLOBAL help='AntTweakBar Test' ");

	TwAddVarRO(bar, "time", TW_TYPE_DOUBLE, &Engine::timer_, " label='Time' precision=2 help='Time (in seconds).' ");

	TwAddVarRO(bar, "fps", TW_TYPE_DOUBLE, &Engine::fps_, " label='FPS' precision=2 help='Frame Per Second' ");
	TwAddVarRO(bar, "delta time", TW_TYPE_DOUBLE, &Engine::delta_time_, " label='Delta Time' precision=4 help='Delta Time (in seconds).' ");
	
	TwAddVarRO(bar, "regulated fps", TW_TYPE_DOUBLE, &Engine::regulated_fps_, " label='Regulated FPS' precision=2 help='Frame Per Second' ");
	TwAddVarRO(bar, "regulated delta time", TW_TYPE_DOUBLE, &Engine::regulated_delta_time_, " label='Regulated Delta Time' precision=4 help='Delta Time (in seconds).' ");
	
	TwAddVarRO(bar, "camera position", TW_TYPE_DIR3F, &Camera::Main()->position_, "");
	TwAddVarRO(bar, "camera orientation", TW_TYPE_QUAT4F, &Camera::Main()->orientation_, "");
	TwAddVarRO(bar, "camera forward", TW_TYPE_DIR3F, &Camera::Main()->forward_, "");
	TwAddVarRO(bar, "camera right", TW_TYPE_DIR3F, &Camera::Main()->right_, "");
	TwAddVarRO(bar, "camera up", TW_TYPE_DIR3F, &Camera::Main()->up_, "");

	GLuint vbo;
	glGenBuffers(1, &vbo);

	glGenVertexArrays(1, &vao);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	
	Vertex2<glm::vec3, glm::vec3> data[36] = {
	{ { -1.0f, -1.0f, -1.0f }, {1.0f, 0.0f, 0.0f} }, 
	{ { 1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { -1.0f, 1.0f, -1.0f }, {1.0f, 0.0f, 0.0f} },
	{ { 1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { 1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { -1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { -1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { 1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { -1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { 1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { -1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	};

	glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
	Vertex2<glm::vec3, glm::vec3>::DescribeLayout();

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glEnable(GL_DEPTH_TEST);

	prog = new ShaderProgram();
	prog->AddShader(GL_VERTEX_SHADER, "shaders\\debug_shader.glsl", "#define _VERTEX_S_");
	prog->AddShader(GL_FRAGMENT_SHADER, "shaders\\debug_shader.glsl", "#define _FRAGMENT_S_");
	prog->Link();

	EventChannel<Engine::RenderEvent>::Add(TestRender);

	Engine::MainLoop();
	Engine::Terminate();

	exit(EXIT_SUCCESS);
}