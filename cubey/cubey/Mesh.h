#pragma once

#include "Vertex.h"
#include "Transform.h"
#include "ShaderProgram.h"
#include "Camera.h"

namespace cubey {
	class Mesh {
	public:
		//Mesh() : vao_(0), vbo_(0), vertices_count_(0), draw_mode_(GL_POINTS) {}
		Mesh(GLsizei vertices_count, GLenum draw_mode) : vao_(0), vbo_(0), vertices_count_(vertices_count), draw_mode_(draw_mode) {}

		~Mesh() {
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

		class SimpleMeshInstance* CreateSimpleInstance(ShaderProgram* program, const std::string& u_mvp_mat_name,
			Camera* camera = Camera::Main(), const Transform& transform = Transform());

		class MeshInstance* CreateInstance(ShaderProgram* program, const std::string& u_mvp_mat_name, const std::string& u_normal_mat_name,
			Camera* camera = Camera::Main(), const Transform& transform = Transform());

		void Draw();

	private:
		GLenum draw_mode_;
		GLsizei vertices_count_;
		GLuint vao_;
		GLuint vbo_;
	};

	class SimpleMeshInstance {
	public:
		SimpleMeshInstance(Mesh* mesh, ShaderProgram* program, int u_mvp_mat_location,
			Camera* camera = Camera::Main(), const Transform& transform = Transform()) :
			mesh_(mesh),
			program_(program),
			u_mvp_mat_location_(u_mvp_mat_location),
			camera_(camera),
			transform_(transform) {
		}

		void Draw();

		Transform transform_;
	private:
		Mesh* mesh_;
		Camera* camera_;
		ShaderProgram* program_;
		int u_mvp_mat_location_;
	};

	class MeshInstance {
	public:
		MeshInstance(Mesh* mesh, ShaderProgram* program, int u_mvp_mat_location, int u_normal_mat_location,
			Camera* camera = Camera::Main(), const Transform& transform = Transform()) :
			mesh_(mesh),
			program_(program),
			u_mvp_mat_location_(u_mvp_mat_location),
			u_normal_mat_location_(u_normal_mat_location),
			camera_(camera),
			transform_(transform) {
		}

		void Draw();

		Transform transform_;
	private:
		Mesh* mesh_;
		Camera* camera_;
		ShaderProgram* program_;
		int u_mvp_mat_location_;
		int u_normal_mat_location_;
	};

	
}