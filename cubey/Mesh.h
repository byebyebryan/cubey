#pragma once

#include "Vertex.h"
#include "Transform.h"

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
		static Mesh* Create(T* vertices, GLsizei vertices_count, GLenum draw_mode, GLenum buffer_uasge = GL_STATIC_DRAW) {
			Mesh* new_mesh = new Mesh(vertices_count, draw_mode);
			
			glGenBuffers(1, &(new_mesh->vbo_));
			glGenVertexArrays(1, &(new_mesh->vao_));

			glBindVertexArray(new_mesh->vao_);
			glBindBuffer(GL_ARRAY_BUFFER, new_mesh->vbo_);

			glBufferData(GL_ARRAY_BUFFER, sizeof(T) * vertices_count, vertices, buffer_uasge);
			vertices->DescribeLayout();

			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			return new_mesh;
		}

		void Draw() {
			glBindVertexArray(vao_);
			glDrawArrays(draw_mode_, 0, vertices_count_);
			glBindVertexArray(0);
		}

		glm::mat4 model_mat() { return transform_.transformation_mat(); }
	private:
		Transform transform_;
		GLenum draw_mode_;
		GLsizei vertices_count_;
		GLuint vao_;
		GLuint vbo_;
	};
}