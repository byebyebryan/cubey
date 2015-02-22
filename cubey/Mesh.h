#pragma once

#include "Vertex.h"
#include "Transform.h"
#include "ShaderProgram.h"
#include "Camera.h"

namespace cubey {
	class Mesh {
	public:

		Mesh(GLsizei vertices_count, GLenum draw_mode) : vao_(0), vbo_(0),
			vertices_count_(vertices_count), draw_mode_(draw_mode) {
		}

		virtual ~Mesh() {
			if (vao_) {
				glDeleteVertexArrays(1, &vao_);
			}
			if (vbo_) {
				glDeleteBuffers(1, &vbo_);
			}
		}

		template<typename T>
		static Mesh* Create(std::vector<T> vertices, GLenum draw_mode, GLenum buffer_uasge = GL_STATIC_DRAW) {
			Mesh* new_mesh = new Mesh(vertices.size(), draw_mode);

			glGenBuffers(1, &(new_mesh->vbo_));
			glGenVertexArrays(1, &(new_mesh->vao_));

			glBindVertexArray(new_mesh->vao_);
			glBindBuffer(GL_ARRAY_BUFFER, new_mesh->vbo_);

			glBufferData(GL_ARRAY_BUFFER, sizeof(T) * vertices.size(), vertices.data(), buffer_uasge);
			T::DescribeLayout();

			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			return new_mesh;
		}

		class MeshInstance* CreateInstance(ShaderProgram* program, const std::string& u_mvp_mat_name = "", const std::string& u_normal_mat_name = "",
			Camera* camera = MainCamera::Get(), const Transform& transform = Transform());

		virtual void Draw() {
			glBindVertexArray(vao_);
			glDrawArrays(draw_mode_, 0, vertices_count_);
			glBindVertexArray(0);
		}

	protected:
		GLenum draw_mode_;
		GLsizei vertices_count_;
		GLuint vao_;
		GLuint vbo_;
	};

	class MeshIndexed : public Mesh {
	public:

		MeshIndexed(GLsizei vertices_count, GLsizei indices_count, GLenum draw_mode) : Mesh(vertices_count, draw_mode), ibo_(0), indices_count_(indices_count) {
		}

		~MeshIndexed() {
			if (ibo_) {
				glDeleteBuffers(1, &ibo_);
			}
		}

		template<typename T>
		static MeshIndexed* Create(std::vector<T> vertices, std::vector<unsigned int> indices, GLenum draw_mode, GLenum buffer_uasge = GL_STATIC_DRAW) {
			MeshIndexed* new_mesh = new MeshIndexed(vertices.size(), indices.size(), draw_mode);

			glGenBuffers(1, &(new_mesh->vbo_));
			glGenBuffers(1, &(new_mesh->ibo_));
			glGenVertexArrays(1, &(new_mesh->vao_));

			glBindVertexArray(new_mesh->vao_);
			glBindBuffer(GL_ARRAY_BUFFER, new_mesh->vbo_);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, new_mesh->ibo_);

			glBufferData(GL_ARRAY_BUFFER, sizeof(T) * vertices.size(), vertices.data(), buffer_uasge);
			T::DescribeLayout();

			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indices.size(), indices.data(), buffer_uasge);

			glBindVertexArray(0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			return new_mesh;
		}

		class MeshInstance* CreateInstance(ShaderProgram* program, const std::string& u_mvp_mat_name = "", const std::string& u_normal_mat_name = "",
			Camera* camera = MainCamera::Get(), const Transform& transform = Transform());

		void Draw() override {
			glBindVertexArray(vao_);
			glDrawElements(draw_mode_, indices_count_, GL_UNSIGNED_INT, 0);
			glBindVertexArray(0);
		}

	private:
		GLsizei indices_count_;
		GLuint ibo_;
	};

	class MeshInstance {
	public:
		MeshInstance(Mesh* mesh, ShaderProgram* program, int u_mvp_mat_location, int u_normal_mat_location,
			Camera* camera = MainCamera::Get(), const Transform& transform = Transform()) :
			mesh_(mesh),
			program_(program),
			u_mvp_mat_location_(u_mvp_mat_location),
			u_normal_mat_location_(u_normal_mat_location),
			camera_(camera),
			transform_(transform) {
		}

		void Draw() {
			if (u_mvp_mat_location_ != -1) {
				program_->SetUniform(u_mvp_mat_location_, camera_->CalculateMVPMat(transform_.transformation_mat()));
			}
			if (u_normal_mat_location_ != -1) {
				program_->SetUniform(u_normal_mat_location_, camera_->CalculateNormalMat(transform_.transformation_mat()));
			}
			mesh_->Draw();
		}

		Transform transform_;
	private:
		Mesh* mesh_;
		Camera* camera_;
		ShaderProgram* program_;
		int u_mvp_mat_location_;
		int u_normal_mat_location_;
	};

	inline MeshInstance* Mesh::CreateInstance(ShaderProgram* program, const std::string& u_mvp_mat_name /*= ""*/, const std::string& u_normal_mat_name /*= ""*/, Camera* camera /*= MainCamera::Get()*/, const Transform& transform /*= Transform()*/) {
		MeshInstance* new_instance = new MeshInstance(this, program,
			program->GetUniformLocation(u_mvp_mat_name), program->GetUniformLocation(u_normal_mat_name), camera, transform);
		return new_instance;
	}

	inline MeshInstance* MeshIndexed::CreateInstance(ShaderProgram* program, const std::string& u_mvp_mat_name /*= ""*/, const std::string& u_normal_mat_name /*= ""*/, Camera* camera /*= MainCamera::Get()*/, const Transform& transform /*= Transform()*/) {
		MeshInstance* new_instance = new MeshInstance(this, program,
			program->GetUniformLocation(u_mvp_mat_name), program->GetUniformLocation(u_normal_mat_name), camera, transform);
		return new_instance;
	}
}