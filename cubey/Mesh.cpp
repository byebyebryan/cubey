#include "Mesh.h"

namespace cubey {
	MeshInstance* Mesh::CreateInstance(ShaderProgram* program, 
		const std::string& u_mvp_mat_name, const std::string& u_normal_mat_name, 
		Camera* camera /*= Camera::Main()*/, const Transform& transform /*= Transform()*/) {

		MeshInstance* new_instance = new MeshInstance(this, program,
			program->GetUniformLocation(u_mvp_mat_name), program->GetUniformLocation(u_normal_mat_name), camera, transform);
		return new_instance;
	}

	void Mesh::Draw() {
		glBindVertexArray(vao_);
		glDrawArrays(draw_mode_, 0, vertices_count_);
		glBindVertexArray(0);
	}

	void MeshInstance::Draw() {
		program_->SetUniform(u_mvp_mat_location_, camera_->CalculateMVPMat(transform_.transformation_mat()));
		program_->SetUniform(u_normal_mat_location_, camera_->CalculateNormalMat(transform_.transformation_mat()));
		mesh_->Draw();
	}

}

