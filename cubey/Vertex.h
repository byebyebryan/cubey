#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

namespace cubey {

	template<typename T0>
	struct Vertex1 {
		T0 attrib_0;

		static void DescribeLayout() {
			GLint size_0 = VertexAttribInfo::GetSize<T0>();
			GLenum type_0 = VertexAttribInfo::GetType<T0>();
			glVertexAttribPointer(0, size_0, type_0, false, 0, (GLvoid*)0);
			glEnableVertexAttribArray(0);
		}
	};

	template<typename T0, typename T1>
	struct Vertex2 {
		T0 attrib_0;
		T1 attrib_1;

		static void DescribeLayout() {
			GLint size_0 = VertexAttribInfo::GetSize<T0>();
			GLenum type_0 = VertexAttribInfo::GetType<T0>();
			glVertexAttribPointer(0, size_0, type_0, false, sizeof(T0) + sizeof(T1), (GLvoid*)0);
			glEnableVertexAttribArray(0);
			GLint size_1 = VertexAttribInfo::GetSize<T1>();
			GLenum type_1 = VertexAttribInfo::GetType<T1>();
			glVertexAttribPointer(1, size_1, type_1, false, sizeof(T0) + sizeof(T1), (GLvoid*)(sizeof(T0)));
			glEnableVertexAttribArray(1);
		}
	};

	template<typename T0, typename T1, typename T2>
	struct Vertex3 {
		T0 attrib_0;
		T1 attrib_1;
		T2 attrib_2;

		static void DescribeLayout() {
			GLint size_0 = VertexAttribInfo::GetSize<T0>();
			GLenum type_0 = VertexAttribInfo::GetType<T0>();
			glVertexAttribPointer(0, size_0, type_0, false, sizeof(T0) + sizeof(T1) + sizeof(T2), (GLvoid*)0);
			glEnableVertexAttribArray(0);
			GLint size_1 = VertexAttribInfo::GetSize<T1>();
			GLenum type_1 = VertexAttribInfo::GetType<T1>();
			glVertexAttribPointer(1, size_1, type_1, false, sizeof(T0) + sizeof(T1) + sizeof(T2), (GLvoid*)(sizeof(T0)));
			glEnableVertexAttribArray(1);
			GLint size_2 = VertexAttribInfo::GetSize<T2>();
			GLenum type_2 = VertexAttribInfo::GetType<T2>();
			glVertexAttribPointer(2, size_1, type_1, false, sizeof(T0) + sizeof(T1) + sizeof(T2), (GLvoid*)(sizeof(T0) + sizeof(T1)));
			glEnableVertexAttribArray(2);
		}
	};

	class VertexAttribInfo {
	public:
		template<typename T>
		static int GetSize() { return 0; }
		template<>
		static int GetSize<glm::vec2>() { return 2; }
		template<>
		static int GetSize<glm::vec3>() { return 3; }
		template<>
		static int GetSize<glm::vec4>() { return 4; }

		template<typename T>
		static GLenum GetType() { return GL_INVALID_VALUE; }
		template<>
		static GLenum GetType<glm::vec2>() { return GL_FLOAT; }
		template<>
		static GLenum GetType<glm::vec3>() { return GL_FLOAT; }
		template<>
		static GLenum GetType<glm::vec4>() { return GL_FLOAT; }
	};
	
}