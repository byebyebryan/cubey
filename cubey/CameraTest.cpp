#include "CameraTest.h"

namespace cubey {

	void CameraTest::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glClearColor(0.4, 0.4, 0.4, 1);

		prog = new ShaderProgram();
		prog->AddShader(GL_VERTEX_SHADER, "shaders\\debug_shader.glsl", "#define _VERTEX_S_");
		prog->AddShader(GL_FRAGMENT_SHADER, "shaders\\debug_shader.glsl", "#define _FRAGMENT_S_");
		prog->Link();

		mesh = Mesh::Create(PrimitiveFactory::AxisIndicator(), GL_TRIANGLES);
		mesh_instance = mesh->CreateInstance(prog, "u_mvp_mat", "u_normal_mat");
	}

	void CameraTest::Render() {
		glm::vec3 ambient_light = glm::vec3(0.5f, 0.5f, 0.5f);
		glm::vec3 directional_light = glm::vec3(0.5f, 0.5f, 0.5f);
		glm::vec4 directional_light_dir = glm::vec4(-0.5f, -1.0f, -0.25f, 0.0f);
		glm::vec3 directional_light_dir_es = glm::vec3(glm::normalize(Camera::Main()->view_mat() * directional_light_dir));
		prog->SetUniform("u_directional_light_direction", directional_light_dir_es);

		prog->Activate();
		prog->SetUniform("u_ambient_light_color", ambient_light);
		prog->SetUniform("u_directional_light_color", directional_light);

		mesh_instance->Draw();
	}

}