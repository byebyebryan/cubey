
#define GLM_FORCE_PURE

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/ext.hpp"

#include <stdlib.h>
#include <stdio.h>

#include <iostream>
#include <thread>
#include <chrono>

#include "Engine.h"
#include "Shader.h"
#include "Vertex.h"
#include "Camera.h"
#include "UI.h"

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
	Camera::Main()->Init();
	UI::Main()->Init();

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