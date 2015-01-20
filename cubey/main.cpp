
#define GLM_FORCE_PURE

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"

#include "glm/gtx/normal.hpp"

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
#include "Input.h"

using namespace cubey;

GLuint vao = 0;
ShaderProgram* prog = nullptr;

glm::mat4 model_mat = glm::mat4();

void TestRender(const Engine::RenderEvent& e) {

	//model_mat = glm::translate(model_mat, glm::vec3(0.0f, 0.0f, 0.01f));
	glm::mat4 mvp_mat = Camera::Main()->MVPMat(model_mat);
	glm::mat3 normal_mat = Camera::Main()->NormalMat(model_mat);

	glm::vec3 ambient_light = glm::vec3(0.5f, 0.5f, 0.5f);
	glm::vec3 directional_light = glm::vec3(0.5f, 0.5f, 0.5f);
	glm::vec4 directional_light_dir = glm::vec4(0.5f, 1.0f, 0.0f, 0.0f);

	glm::vec3 directional_light_dir_es = glm::vec3(glm::normalize(Camera::Main()->ViewMat() * directional_light_dir));

	prog->Activate();
	prog->SetUniform("u_mvp_mat", mvp_mat);
	prog->SetUniform("u_normal_mat", normal_mat);
	prog->SetUniform("u_ambient_light_color", ambient_light);
	prog->SetUniform("u_directional_light_direction", directional_light_dir_es);
	prog->SetUniform("u_directional_light_color", directional_light);

	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

int main(void) {
	Engine::Init();

	Input::Main()->Init();
	UI::Main()->Init();
	Camera::Main()->Init();

	GLuint vbo;
	glGenBuffers(1, &vbo);

	glGenVertexArrays(1, &vao);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	
	//Vertex2<glm::vec3, glm::vec3> data[18] = {
	//{ { -1.0f, -1.0f, -1.0f }, {1.0f, 0.0f, 0.0f} }, 
	//{ { 1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	//{ { -1.0f, 1.0f, -1.0f }, {1.0f, 0.0f, 0.0f} },
	//{ { 1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	//{ { 1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	//{ { -1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	///*{ { -1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	//{ { 1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	//{ { -1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	//{ { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	//{ { 1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	//{ { -1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },*/
	//{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	///*{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
	//{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },*/
	//{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	///*{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	//{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },*/
	//};

	Vertex3<glm::vec3, glm::vec3, glm::vec3> data[36] = {
		{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 0.0f, -0.2f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },

		{ { 0.0f, -0.2f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { 0.0f, 0.0f, -0.2f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },

		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -0.2f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { -0.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },

		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, -0.2f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },

		{ { 0.0f, 0.0f, -0.2f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -0.15f, -0.15f, -0.15f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },

		{ { -0.15f, -0.15f, -0.15f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, -0.2f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { -0.15f, -0.15f, -0.15f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, 0.0f, -0.2f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },

		{ { -0.2f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -0.15f, -0.15f, -0.15f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { -0.15f, -0.15f, -0.15f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -0.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },

		{ { 0.0f, -0.2f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { -0.15f, -0.15f, -0.15f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	};

	int a, b, c;
	a = 18;
	b = a + 1, c = a + 2;
	data[a].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[b].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[c].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);

	a += 3;  b = a + 1, c = a + 2;
	data[a].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[b].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[c].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);

	a += 3;  b = a + 1, c = a + 2;
	data[a].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[b].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[c].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);

	a += 3;  b = a + 1, c = a + 2;
	data[a].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[b].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[c].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);

	a += 3;  b = a + 1, c = a + 2;
	data[a].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[b].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[c].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);

	a += 3;  b = a + 1, c = a + 2;
	data[a].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[b].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);
	data[c].attrib_2 = glm::triangleNormal(data[a].attrib_0, data[b].attrib_0, data[c].attrib_0);


	glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
	data->DescribeLayout();

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	prog = new ShaderProgram();
	prog->AddShader(GL_VERTEX_SHADER, "shaders\\debug_shader.glsl", "#define _VERTEX_S_");
	prog->AddShader(GL_FRAGMENT_SHADER, "shaders\\debug_shader.glsl", "#define _FRAGMENT_S_");
	prog->Link();

	EventChannel<Engine::RenderEvent>::DirtyAdd(TestRender);

	Engine::MainLoop();
	Engine::Terminate();

	exit(EXIT_SUCCESS);
}