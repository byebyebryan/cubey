#pragma once

#include "Vertex.h"

namespace cubey {

	template<typename... TX>
	using VertexArray = typename Vertex<TX...>::Array;

	class VertexArrayHelper {
	public:
		template<typename T>
		static void AppendArray(std::vector<T>& vertex_array_0, const std::vector<T>& vertex_array_1) {
			vertex_array_0.insert(vertex_array_0.end(), vertex_array_1.begin(), vertex_array_1.end());
		}

		template<typename T, typename... TX>
		static typename Vertex<TX..., T>::Array AppendAttrib(std::vector<Vertex<TX...>>& vertex_array_0, const T& attrib) {
			Vertex<TX..., T>::Array new_vertex_array;
			for (auto& vert : vertex_array_0) {
				new_vertex_array.push_back(vert.AppendAttrib<T>(attrib));
			}
			return new_vertex_array;
		}

		template<typename T>
		static typename T::ParentType::Array PopAttrib(std::vector<T>& vertex_array_0) {
			T::ParentType::Array new_vertex_array;
			for (auto& vert : vertex_array_0) {
				new_vertex_array.push_back(vert.PopAttrib());
			}
			return new_vertex_array;
		}

		template<typename... TX>
		static void ApplyTransform(std::vector<Vertex<glm::vec3, TX...>>& vertex_array, glm::mat4 transformation_mat) {
			for (auto& vertex : vertex_array) {
				glm::vec4 pos = glm::vec4(vertex.position, 1.0f);
				pos = transformation_mat * pos;
				vertex.position = glm::vec3(pos);
			}
		}

		template<typename... TX>
		static void ApplyTransform(std::vector<Vertex<glm::vec4, TX...>>& vertex_array, glm::mat4 transformation_mat) {
			for (auto& vertex : vertex_array) {
				vertex.position = transformation_mat * vertex.position;
			}
		}

		template<typename T>
		static void TriangulateNormal(std::vector<T>& vertex_array, glm::vec3 T::*normal_attrib_ptr) {
			int num_triangles = vertex_array.size() / 3;
			for (int i = 0; i < num_triangles; i++) {
				glm::vec3 i_normal = glm::triangleNormal(glm::vec3(vertex_array[i * 3].position), glm::vec3(vertex_array[i * 3 + 1].position), glm::vec3(vertex_array[i * 3 + 2].position));
				vertex_array[i * 3].*normal_attrib_ptr = i_normal;
				vertex_array[i * 3 + 1].*normal_attrib_ptr = i_normal;
				vertex_array[i * 3 + 2].*normal_attrib_ptr = i_normal;
			}
		}

		template<typename T, typename TC>
		static void ChangeColor(std::vector<T>& vertex_array, TC T::*color_attrib_ptr, const TC& color) {
			for (auto& vertex : vertex_array) {
				vertex.*color_attrib_ptr = color;
			}
		}
	};
}