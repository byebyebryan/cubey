#include "GL/glew.h"

#include "Engine.h"
#include "Camera.h"
#include "UI.h"
#include "Input.h"

#include "Shader.h"
#include "Vertex.h"

using namespace cubey;

GLuint vao = 0;
ShaderProgram* prog = nullptr;

glm::mat4 model_mat = glm::mat4();

void TestRender(const Engine::RenderEvent& e) {

	//model_mat = glm::translate(model_mat, glm::vec3(0.0f, 0.0f, 0.01f));
	glm::mat4 mvp_mat = Camera::Main()->CalculateMVPMat(model_mat);
	glm::mat3 normal_mat = Camera::Main()->CalculateNormalMat(model_mat);

	glm::vec3 ambient_light = glm::vec3(0.5f, 0.5f, 0.5f);
	glm::vec3 directional_light = glm::vec3(0.5f, 0.5f, 0.5f);
	glm::vec4 directional_light_dir = glm::vec4(-0.5f, -1.0f, -0.25f, 0.0f);

	glm::vec3 directional_light_dir_es = glm::vec3(glm::normalize(Camera::Main()->view_mat() * directional_light_dir));

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
	Camera::Main()->Init();
	UI::Main()->Init();

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
		{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, 0.0f, -0.2f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },

		{ { 0.0f, -0.2f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },

		{ { 0.0f, 0.0f, -0.2f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },

		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -0.2f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },

		{ { -0.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, -0.2f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { 0.0f, 0.0f, -0.2f }, { 1.0f, 0.0f, 0.0f } },
		{ { -0.15f, -0.15f, -0.15f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },

		{ { -0.15f, -0.15f, -0.15f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.0f, -0.2f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },

		{ { -0.15f, -0.15f, -0.15f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 0.0f, -0.2f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },

		{ { -0.2f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -0.15f, -0.15f, -0.15f }, { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },

		{ { -0.15f, -0.15f, -0.15f }, { 0.0f, 0.0f, 1.0f } },
		{ { -0.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { 0.0f, -0.2f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -0.15f, -0.15f, -0.15f }, { 0.0f, 0.0f, 1.0f } },
		{ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	};

	VertexListHelper::CalculateNormals(data, 36, data->attrib_ptr_0(), data->attrib_ptr_2());

	glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
	data->DescribeLayout();


	glm::vec3
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