#include "MarchingCubesDemo.h"

#include "TriangleTable.h"

namespace cubey {


	void MarchingCubesDemo::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glClearColor(0.4, 0.4, 0.4, 1);

		ShaderManager::BufferCleaner shader_cleaner;

		cubes_render_prog_ = ShaderManager::Main()->CreateProgram("marching_cubes_render.VS.GS.FS");

		glGenBuffers(1, &ssbo_);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(triangle_table), triangle_table, GL_STATIC_READ);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_);

		Vertex<VPosition3>::Array verts;

		for (int x = 0; x < 64; x++) {
			for (int y = 0; y < 32; y++) {
				for (int z = 0; z < 64; z++) {
					verts.push_back(Vertex<VPosition3>{ { -0.5f + x / 64.0f, -0.125f + y / 64.0f, -0.5f + z / 64.0f } });
				}
			}
		}

		mesh_ = Mesh::Create(verts, GL_POINTS);
		mesh_instance_ = mesh_->CreateInstance(cubes_render_prog_, "u_mvp_mat");

		cubes_render_prog_->Activate();
		cubes_render_prog_->SetUniform("u_block_resolution", 64.0f);
		
	}

	void MarchingCubesDemo::Update(float delta_time) {

	}

	void MarchingCubesDemo::Render() {

		glm::vec3 ambient_light = glm::vec3(0.5f, 0.5f, 0.5f);
		glm::vec3 directional_light = glm::vec3(0.5f, 0.5f, 0.5f);
		glm::vec4 directional_light_dir = glm::vec4(-0.5f, -1.0f, -0.25f, 0.0f);
		glm::vec3 directional_light_dir_es = glm::vec3(glm::normalize(Camera::Main()->view_mat() * directional_light_dir));
		

		cubes_render_prog_->Activate();

		cubes_render_prog_->SetUniform("u_time", Time::time_since_start() / 10.0f);

		cubes_render_prog_->SetUniform("u_normal_mat", Camera::Main()->CalculateNormalMat(glm::mat4()));

		cubes_render_prog_->SetUniform("u_directional_light_direction", directional_light_dir_es);
		cubes_render_prog_->SetUniform("u_ambient_light_color", ambient_light);
		cubes_render_prog_->SetUniform("u_directional_light_color", directional_light);

		mesh_instance_->Draw();
	}

}