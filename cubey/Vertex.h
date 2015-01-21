#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include "glm/gtx/normal.hpp"

namespace cubey {

	template<typename T0>
	struct Vertex1 {
		T0 attrib_0;

		static T0 Vertex1::* attrib_ptr_0() {
			return &Vertex1::attrib_0;
		}

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

		static T0 Vertex2::* attrib_ptr_0() {
			return &Vertex2::attrib_0;
		}
		static T1 Vertex2::* attrib_ptr_1() {
			return &Vertex2::attrib_1;
		}

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

		static T0 Vertex3::* attrib_ptr_0() {
			return &Vertex3::attrib_0;
		}
		static T1 Vertex3::* attrib_ptr_1() {
			return &Vertex3::attrib_1;
		}
		static T2 Vertex3::* attrib_ptr_2() {
			return &Vertex3::attrib_2;
		}

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
			glVertexAttribPointer(2, size_2, type_2, false, sizeof(T0) + sizeof(T1) + sizeof(T2), (GLvoid*)(sizeof(T0) + sizeof(T1)));
			glEnableVertexAttribArray(2);
		}
	};

	template<typename T0, typename T1, typename T2, typename T3>
	struct Vertex4 {
		T0 attrib_0;
		T1 attrib_1;
		T2 attrib_2;
		T3 attrib_3;

		static T0 Vertex4::* attrib_ptr_0() {
			return &Vertex4::attrib_0;
		}
		static T1 Vertex4::* attrib_ptr_1() {
			return &Vertex4::attrib_1;
		}
		static T2 Vertex4::* attrib_ptr_2() {
			return &Vertex4::attrib_2;
		}
		static T3 Vertex4::* attrib_ptr_3() {
			return &Vertex4::attrib_3;
		}

		static void DescribeLayout() {
			GLint size_0 = VertexAttribInfo::GetSize<T0>();
			GLenum type_0 = VertexAttribInfo::GetType<T0>();
			glVertexAttribPointer(0, size_0, type_0, false, sizeof(T0) + sizeof(T1) + sizeof(T2) + sizeof(T3), (GLvoid*)0);
			glEnableVertexAttribArray(0);
			GLint size_1 = VertexAttribInfo::GetSize<T1>();
			GLenum type_1 = VertexAttribInfo::GetType<T1>();
			glVertexAttribPointer(1, size_1, type_1, false, sizeof(T0) + sizeof(T1) + sizeof(T2) + sizeof(T3), (GLvoid*)(sizeof(T0)));
			glEnableVertexAttribArray(1);
			GLint size_2 = VertexAttribInfo::GetSize<T2>();
			GLenum type_2 = VertexAttribInfo::GetType<T2>();
			glVertexAttribPointer(2, size_2, type_2, false, sizeof(T0) + sizeof(T1) + sizeof(T2) + sizeof(T3), (GLvoid*)(sizeof(T0) + sizeof(T1)));
			glEnableVertexAttribArray(2);
			GLint size_3 = VertexAttribInfo::GetSize<T3>();
			GLenum type_3 = VertexAttribInfo::GetType<T3>();
			glVertexAttribPointer(3, size_3, type_3, false, sizeof(T0) + sizeof(T1) + sizeof(T2) + sizeof(T3), (GLvoid*)(sizeof(T0) + sizeof(T1) + sizeof(T2)));
			glEnableVertexAttribArray(3);
		}
	};

	class VertexArrayHelper {
	public:
		template<typename T0, typename T1>
		static void CalculateNormals(Vertex2<T0, T1>* verts, int size, glm::vec3 Vertex2<T0, T1>::*pos_attrib, glm::vec3 Vertex2<T0, T1>::*normal_attrib) {
			CalculateNormalsImpl(verts, size, pos_attrib, normal_attrib);
		}
		template<typename T0, typename T1>
		static void CalculateNormals(Vertex2<T0, T1>* verts, int size, glm::vec4 Vertex2<T0, T1>::*pos_attrib, glm::vec3 Vertex2<T0, T1>::*normal_attrib) {
			CalculateNormalsImpl(verts, size, pos_attrib, normal_attrib);
		}
		template<typename T0, typename T1, typename T2>
		static void CalculateNormals(Vertex3<T0, T1, T2>* verts, int size, glm::vec3 Vertex3<T0, T1, T2>::*pos_attrib, glm::vec3 Vertex3<T0, T1, T2>::*normal_attrib) {
			CalculateNormalsImpl(verts, size, pos_attrib, normal_attrib);
		}
		template<typename T0, typename T1, typename T2>
		static void CalculateNormals(Vertex3<T0, T1, T2>* verts, int size, glm::vec4 Vertex3<T0, T1, T2>::*pos_attrib, glm::vec3 Vertex3<T0, T1, T2>::*normal_attrib) {
			CalculateNormalsImpl(verts, size, pos_attrib, normal_attrib);
		}
		template<typename T0, typename T1, typename T2, typename T3>
		static void CalculateNormals(Vertex4<T0, T1, T2, T3>* verts, int size, glm::vec3 Vertex4<T0, T1, T2, T3>::*pos_attrib, glm::vec3 Vertex4<T0, T1, T2, T3>::*normal_attrib) {
			CalculateNormalsImpl(verts, size, pos_attrib, normal_attrib);
		}
		template<typename T0, typename T1, typename T2, typename T3>
		static void CalculateNormals(Vertex4<T0, T1, T2, T3>* verts, int size, glm::vec4 Vertex4<T0, T1, T2, T3>::*pos_attrib, glm::vec3 Vertex4<T0, T1, T2, T3>::*normal_attrib) {
			CalculateNormalsImpl(verts, size, pos_attrib, normal_attrib);
		}

	private:
		template<typename T>
		static void CalculateNormalsImpl(T* verts, int size, glm::vec3 T::*pos_attrib, glm::vec3 T::*normal_attrib) {
			int num_triangles = size / 3;
			for (int i = 0; i < num_triangles; i++) {
				glm::vec3 i_normal = glm::triangleNormal(verts[i * 3].*pos_attrib, verts[i * 3 + 1].*pos_attrib, verts[i * 3 + 2].*pos_attrib);
				verts[i * 3].*normal_attrib = i_normal;
				verts[i * 3 + 1].*normal_attrib = i_normal;
				verts[i * 3 + 2].*normal_attrib = i_normal;
			}
		}
		template<typename T>
		static void CalculateNormalsImpl(T* verts, int size, glm::vec4 T::*pos_attrib, glm::vec3 T::*normal_attrib) {
			int num_triangles = size / 3;
			for (int i = 0; i < num_triangles; i++) {
				glm::vec3 i_normal = glm::triangleNormal(verts[i * 3].*pos_attrib, verts[i * 3 + 1].*pos_attrib, verts[i * 3 + 2].*pos_attrib);
				verts[i * 3].*normal_attrib = i_normal;
				verts[i * 3 + 1].*normal_attrib = i_normal;
				verts[i * 3 + 2].*normal_attrib = i_normal;
			}
		}
	};

	class VertexAttribInfo {
	public:
		template<typename T>
		static int GetSize() { return 0; }
		template<>
		static int GetSize<int>() { return 1; }
		template<>
		static int GetSize<float>() { return 1; }
		template<>
		static int GetSize<glm::vec2>() { return 2; }
		template<>
		static int GetSize<glm::vec3>() { return 3; }
		template<>
		static int GetSize<glm::vec4>() { return 4; }

		template<typename T>
		static GLenum GetType() { return GL_INVALID_VALUE; }
		template<>
		static GLenum GetType<int>() { return GL_INT; }
		template<>
		static GLenum GetType<float>() { return GL_FLOAT; }
		template<>
		static GLenum GetType<glm::vec2>() { return GL_FLOAT; }
		template<>
		static GLenum GetType<glm::vec3>() { return GL_FLOAT; }
		template<>
		static GLenum GetType<glm::vec4>() { return GL_FLOAT; }
	};
	
}