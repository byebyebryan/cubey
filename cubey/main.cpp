#include "GL/glew.h"

#include "Engine.h"
#include "Camera.h"
#include "UI.h"
#include "Input.h"

#include "Shader.h"
#include "Vertex.h"
#include "Mesh.h"

using namespace cubey;

GLuint vao = 0;
ShaderProgram* prog = nullptr;

glm::mat4 model_mat1 = glm::mat4();
glm::mat4 model_mat2 = glm::translate(glm::vec3(2.0f, 2.0f, 2.0f));
Mesh* mesh1 = nullptr;
Mesh* mesh2 = nullptr;
MeshInstance* mesh_instance1 = nullptr;
MeshInstance* mesh_instance2 = nullptr;

void TestRender(const Engine::RenderEvent& e) {

	glm::vec3 ambient_light = glm::vec3(0.5f, 0.5f, 0.5f);
	glm::vec3 directional_light = glm::vec3(0.5f, 0.5f, 0.5f);
	glm::vec4 directional_light_dir = glm::vec4(-0.5f, -1.0f, -0.25f, 0.0f);
	glm::vec3 directional_light_dir_es = glm::vec3(glm::normalize(Camera::Main()->view_mat() * directional_light_dir));
	prog->SetUniform("u_directional_light_direction", directional_light_dir_es);

	prog->Activate();
	prog->SetUniform("u_ambient_light_color", ambient_light);
	prog->SetUniform("u_directional_light_color", directional_light);
	
	prog->SetUniform("u_mvp_mat", Camera::Main()->CalculateMVPMat(model_mat1));
	prog->SetUniform("u_normal_mat", Camera::Main()->CalculateNormalMat(model_mat1));
	mesh1->Draw();

	prog->SetUniform("u_mvp_mat", Camera::Main()->CalculateMVPMat(model_mat2));
	prog->SetUniform("u_normal_mat", Camera::Main()->CalculateNormalMat(model_mat2));
	mesh2->Draw();

	mesh_instance1->Draw();
	mesh_instance2->Draw();
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

	VertexArrayHelper::CalculateNormals(data, 36, data->attrib_ptr_0(), data->attrib_ptr_2());

	mesh1 = Mesh::Create(data, 36, GL_TRIANGLES);

	Vertex3<glm::vec3, glm::vec3, glm::vec3> data2[36] = {
		{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
		
		{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

		{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		
		{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		
		{ { 1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		
		{ { -1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		
		{ { 1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		
		{ { -1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
		
		{ { -1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
		
		{ { 1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
		
		{ { 1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
	};

	VertexArrayHelper::CalculateNormals(data2, 36, data2->attrib_ptr_0(), data2->attrib_ptr_2());

	mesh2 = Mesh::Create(data2, 36, GL_TRIANGLES);
	
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	prog = new ShaderProgram();
	prog->AddShader(GL_VERTEX_SHADER, "shaders\\debug_shader.glsl", "#define _VERTEX_S_");
	prog->AddShader(GL_FRAGMENT_SHADER, "shaders\\debug_shader.glsl", "#define _FRAGMENT_S_");
	prog->Link();

	mesh_instance1 = mesh2->CreateInstance(prog, "u_mvp_mat", "u_normal_mat");
	mesh_instance1->transform_.Translate(glm::vec3(2.0f, 0.0f, 0.0f));
	
	mesh_instance2 = mesh2->CreateInstance(prog, "u_mvp_mat", "u_normal_mat");
	mesh_instance2->transform_.Translate(glm::vec3(0.0f, 0.0f, 2.0f));

	EventChannel<Engine::RenderEvent>::DirtyAdd(TestRender);

	Engine::MainLoop();
	Engine::Terminate();

	exit(EXIT_SUCCESS);
}