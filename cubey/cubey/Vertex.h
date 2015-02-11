#pragma once

#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "glm/gtx/normal.hpp"

namespace cubey {

	template<typename T0 = glm::vec3, typename... TX>
	struct Vertex {
		T0 position;

		typedef std::vector<Vertex> ArrayType;

		static void DescribeLayout() {
			GLint size_0 = VertexAttribInfo::GetSize<T0>();
			GLenum type_0 = VertexAttribInfo::GetType<T0>();
			glVertexAttribPointer(0, size_0, type_0, false, 0, (GLvoid*)0);
			glEnableVertexAttribArray(0);
		}
	};

	template<typename T0, typename T1>
	struct Vertex<T0, T1> {
		T0 position;
		T1 attrib_0;

		typedef std::vector<Vertex> ArrayType;

		static T1 Vertex::* attrib_ptr_0() {
			return &Vertex::attrib_0;
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
	struct Vertex < T0, T1, T2 > {
		T0 position;
		T1 attrib_0;
		T2 attrib_1;

		typedef std::vector<Vertex> ArrayType;

		static T1 Vertex::* attrib_ptr_0() {
			return &Vertex::attrib_0;
		}
		static T2 Vertex::* attrib_ptr_1() {
			return &Vertex::attrib_1;
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
	struct Vertex < T0, T1, T2, T3 > {
		T0 position;
		T1 attrib_0;
		T2 attrib_1;
		T3 attrib_2;

		typedef std::vector<Vertex> ArrayType;

		static T1 Vertex::* attrib_ptr_0() {
			return &Vertex::attrib_0;
		}
		static T2 Vertex::* attrib_ptr_1() {
			return &Vertex::attrib_1;
		}
		static T3 Vertex::* attrib_ptr_2() {
			return &Vertex::attrib_2;
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

	template<typename... TX>
	using VertexArray = typename Vertex<TX...>::VertexArray;

	class VertexArrayHelper {
	public:
		template<typename T0, typename T1>
		static void CalculateNormal(std::vector<T0>& vertex_array, T1 T0::*pos_attrib_ptr, glm::vec3 T0::*normal_attrib_ptr) {
			int num_triangles = vertex_array.size() / 3;
			for (int i = 0; i < num_triangles; i++) {
				glm::vec3 i_normal = glm::triangleNormal(vertex_array[i * 3].*pos_attrib_ptr, vertex_array[i * 3 + 1].*pos_attrib_ptr, vertex_array[i * 3 + 2].*pos_attrib_ptr);
				vertex_array[i * 3].*normal_attrib_ptr = i_normal;
				vertex_array[i * 3 + 1].*normal_attrib_ptr = i_normal;
				vertex_array[i * 3 + 2].*normal_attrib_ptr = i_normal;
			}
		}

		template<typename T0, typename T1>
		static void ChangeColor(std::vector<T0>& vertex_array, T1 T0::*color_attrib_ptr, const T1& color) {
			for (T0& vertex : vertex_array) {
				vertex.*color_attrib_ptr = color;
			}
		}

		template<typename T>
		static void ApplyTransform(std::vector<T>& vertex_array, glm::vec3 T::*pos_attrib_ptr, glm::vec3 T::*normal_attrib_ptr, glm::mat4 transformation_mat) {
			for (T& vertex : vertex_array) {
				glm::vec4 pos = glm::vec4(vertex.*pos_attrib_ptr, 1.0f);
				pos = transformation_mat * pos;
				vertex.*pos_attrib_ptr = glm::vec3(pos);
			}
			CalculateNormal(vertex_array, pos_attrib_ptr, normal_attrib_ptr);
		}

		template<typename T>
		static void ApplyTransform(std::vector<T>& vertex_array, glm::vec4 T::*pos_attrib_ptr, glm::vec3 T::*normal_attrib_ptr, glm::mat4 transformation_mat) {
			for (T& vertex : vertex_array) {
				vertex.*pos_attrib_ptr = transformation_mat * vertex.*pos_attrib_ptr;
			}
			CalculateNormal(vertex_array, pos_attrib_ptr, normal_attrib_ptr);
		}

		template<typename T>
		static std::vector<T> CombineArrays(std::vector<T>& vertex_array_0, std::vector<T>& vertex_array_1) {
			std::vector<T> new_array(vertex_array_0);
			new_array.insert(new_array.end(), vertex_array_1.begin(), vertex_array_1.end());
			return new_array;
		}

	};

	class VertexAttribInfo {
	public:
		template<typename T>
		static int GetSize() { return 0; }
		template<>
		static int GetSize<unsigned int>() { return 1; }
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
		static GLenum GetType<unsigned int>() { return GL_UNSIGNED_INT; }
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