#pragma once

#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "glm/gtx/normal.hpp"

namespace cubey {

	typedef glm::vec2 VPosition2;
	typedef glm::vec3 VPosition3;
	typedef glm::vec4 VPosition4;
	typedef glm::vec3 VNormal;
	typedef glm::vec3 VColor3;
	typedef glm::vec4 VColor4;
	typedef glm::vec2 VTexCoord2;
	typedef glm::vec3 VTexCoord3;

	template<typename T0 = glm::vec3, typename... TX>
	struct Vertex {
		T0 position;

		typedef std::vector<Vertex> Array;

		template<typename T1>
		Vertex<T0, T1> AppendAttrib(const T1& attrib) {
			Vertex<T0, T1> vert = { position, attrib };
			return vert;
		}

		static void DescribeLayout(int extra_size = 0) {
			GLint size_0 = VertexAttribInfo::GetSize<T0>();
			GLenum type_0 = VertexAttribInfo::GetType<T0>();
			glVertexAttribPointer(0, size_0, type_0, false, sizeof(T0) + extra_size, (GLvoid*)0);
			glEnableVertexAttribArray(0);
		}
	};

	template<typename T0, typename T1>
	struct Vertex<T0, T1> {
		T0 position;
		T1 attrib_1;

		typedef std::vector<Vertex> Array;
		typedef Vertex<T0> ParentType;

		template<typename T2>
		Vertex<T0, T1, T2> AppendAttrib(const T2& attrib) {
			Vertex<T0, T1, T2> vert = { position, attrib_1, attrib };
			return vert;
		}

		Vertex<T0> PopAttrib() {
			Vertex<T0> vert = { position };
			return vert;
		}

		static T1 Vertex::* attrib_ptr_1() {
			return &Vertex::attrib_1;
		}

		static void DescribeLayout(int extra_size = 0) {

			Vertex<T0>::DescribeLayout(extra_size + sizeof(T1));

			GLint size_1 = VertexAttribInfo::GetSize<T1>();
			GLenum type_1 = VertexAttribInfo::GetType<T1>();
			glVertexAttribPointer(1, size_1, type_1, false, sizeof(T0) + sizeof(T1) + extra_size, (GLvoid*)(sizeof(T0)));
			glEnableVertexAttribArray(1);
		}
	};

	template<typename T0, typename T1, typename T2>
	struct Vertex < T0, T1, T2 > {
		T0 position;
		T1 attrib_1;
		T2 attrib_2;

		typedef std::vector<Vertex> Array;
		typedef Vertex<T0, T1> ParentType;

		template<typename T3>
		Vertex<T0, T1, T2, T3> AppendAttrib(const T3& attrib) {
			Vertex<T0, T1, T2, T3> vert = { position, attrib_1, attrib_2, attrib };
			return vert;
		}

		Vertex<T0, T1> PopAttrib() {
			Vertex<T0, T1> vert = { position, attrib_1 };
			return vert;
		}

		static T1 Vertex::* attrib_ptr_1() {
			return &Vertex::attrib_1;
		}
		static T2 Vertex::* attrib_ptr_2() {
			return &Vertex::attrib_2;
		}

		static void DescribeLayout(int extra_size = 0) {
			Vertex<T0, T1>::DescribeLayout(extra_size + sizeof(T2));

			GLint size_2 = VertexAttribInfo::GetSize<T2>();
			GLenum type_2 = VertexAttribInfo::GetType<T2>();
			glVertexAttribPointer(2, size_2, type_2, false, sizeof(T0) + sizeof(T1) + sizeof(T2) + extra_size, (GLvoid*)(sizeof(T0) + sizeof(T1)));
			glEnableVertexAttribArray(2);
		}
	};

	template<typename T0, typename T1, typename T2, typename T3>
	struct Vertex < T0, T1, T2, T3 > {
		T0 position;
		T1 attrib_1;
		T2 attrib_2;
		T3 attrib_3;

		typedef std::vector<Vertex> Array;
		typedef Vertex<T0, T1, T2> ParentType;

		Vertex<T0, T1, T2> PopAttrib() {
			Vertex<T0, T1, T2> vert = { position, attrib_1, attrib_2 };
			return vert;
		}

		static T2 Vertex::* attrib_ptr_1() {
			return &Vertex::attrib_1;
		}
		static T3 Vertex::* attrib_ptr_2() {
			return &Vertex::attrib_2;
		}
		static T1 Vertex::* attrib_ptr_3() {
			return &Vertex::attrib_3;
		}

		static void DescribeLayout(int extra_size = 0) {
			Vertex<T0, T1, T2>::DescribeLayout(extra_size + sizeof(T3))

			GLint size_3 = VertexAttribInfo::GetSize<T3>();
			GLenum type_3 = VertexAttribInfo::GetType<T3>();
			glVertexAttribPointer(3, size_3, type_3, false, sizeof(T0) + sizeof(T1) + sizeof(T2) + extra_size, (GLvoid*)(sizeof(T0) + sizeof(T1) + sizeof(T2)));
			glEnableVertexAttribArray(3);
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