#include "PrimitiveFactory.h"

#include "glm/gtx/transform.hpp"

namespace cubey {

	MeshIndexed* PrimitiveFactory::UnitXZPlane() {
		static Vertex<VPosition3, VNormal>::Array vertices = {
			{ { -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
			{ { -0.5f, 0.0f, 0.5f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.0f, 0.5f }, { 0.0f, 1.0f, 0.0f } },

			{ { -0.5f, 0.0f, -0.5f }, { 0.0f, -1.0f, 0.0f } },
			{ { 0.5f, 0.0f, -0.5f }, { 0.0f, -1.0f, 0.0f } },
			{ { -0.5f, 0.0f, 0.5f }, { 0.0f, -1.0f, 0.0f } },
			{ { 0.5f, 0.0f, 0.5f }, { 0.0f, -1.0f, 0.0f } },
		};
		static std::vector<unsigned int> indicies = { 0, 1, 2, 2, 1, 3, 4, 6, 5, 5, 6, 7 };
		static MeshIndexed* mesh = MeshIndexed::Create(vertices, indicies, GL_TRIANGLES);
		return mesh;
	}

	MeshIndexed* PrimitiveFactory::UnitCube() {
		static Vertex<VPosition3, VNormal>::Array vertices = {
			{ { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f } },
			{ { 0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f } },
			{ { -0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f } },
			{ { 0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f } },

			{ { -0.5f, -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.5f, -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
			{ { -0.5f, 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.5f, 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },

			{ { -0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f, 0.0f } },
			{ { 0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f, 0.0f } },
			{ { -0.5f, -0.5f, 0.5f }, { 0.0f, -1.0f, 0.0f } },
			{ { 0.5f, -0.5f, 0.5f }, { 0.0f, -1.0f, 0.0f } },

			{ { -0.5f, 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
			{ { -0.5f, 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f } },

			{ { -0.5f, -0.5f, -0.5f }, { -1.0f, 0.0f, 0.0f } },
			{ { -0.5f, -0.5f, 0.5f }, { -1.0f, 0.0f, 0.0f } },
			{ { -0.5f, 0.5f, -0.5f }, { -1.0f, 0.0f, 0.0f } },
			{ { -0.5f, 0.5f, 0.5f }, { -1.0f, 0.0f, 0.0f } },

			{ { 0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, -0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, 0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f } },
		};

		static std::vector<unsigned int> indicies = {
			0, 1, 2, 2, 1, 3,
			4, 6, 5, 5, 6, 7,
			8, 10, 9, 9, 10, 11,
			12, 13, 14, 14, 13, 15,
			16, 18, 17, 17, 18, 19,
			20, 21, 22, 22, 21, 23,
		};

		static MeshIndexed* mesh = MeshIndexed::Create(vertices, indicies, GL_TRIANGLES);
		return mesh;
	}

	MeshIndexed* PrimitiveFactory::UnitSphere(int iterations) {
		static float d = glm::radians(120.0f);
		Vertex<VPosition3, VNormal>::Array vertices = {
			{ { 0.0f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ glm::rotateX(glm::vec3(0.0f, 0.5f, 0.0f), d), glm::rotateX(glm::vec3(0.0f, 1.0f, 0.0f), d) },
			{ glm::rotateY(glm::rotateX(glm::vec3(0.0f, 0.5f, 0.0f), d), d), glm::rotateY(glm::rotateX(glm::vec3(0.0f, 1.0f, 0.0f), d), d) },
			{ glm::rotateY(glm::rotateX(glm::vec3(0.0f, 0.5f, 0.0f), d), d*2.0f), glm::rotateY(glm::rotateX(glm::vec3(0.0f, 1.0f, 0.0f), d), d*2.0f) },
		};
		std::vector<unsigned int> indicies = { 0, 2, 1, 0, 3, 2, 0, 1, 3, 1, 2, 3 };
		for (int i = 0; i < iterations; i++) {
			CircularizationIterate(vertices, indicies);
		}
		MeshIndexed* mesh = MeshIndexed::Create(vertices, indicies, GL_TRIANGLES);
		return mesh;
	}

	Mesh* PrimitiveFactory::FullScreenQuad() {
		static Vertex<VPosition2, VTexCoord2>::Array vertices = {
			{ { -1.0f, 1.0f }, { 0.0f, 1.0f } },
			{ { -1.0f, -1.0f }, { 0.0f, 0.0f } },
			{ { 1.0f, 1.0f }, { 1.0f, 1.0f } },
			{ { 1.0f, -1.0f }, { 1.0f, 0.0f } },
		};
		static Mesh* mesh = Mesh::Create(vertices, GL_TRIANGLE_STRIP);
		return mesh;
	}

	void PrimitiveFactory::CircularizationIterate(Vertex<VPosition3, VNormal>::Array& vertices, std::vector<unsigned int>& indicies) {
		vertices.reserve(vertices.size() * 2);
		indicies.reserve(indicies.size() * 2);
		int num_triangles = indicies.size() / 3;
		for (int i = 0; i < num_triangles; i++) {
			int i_0 = indicies[i * 3];
			int i_1 = indicies[i * 3 + 1];
			int i_2 = indicies[i * 3 + 2];
			glm::vec3 p_3 = glm::normalize((vertices[i_0].position + vertices[i_1].position) / 2.0f);
			glm::vec3 p_4 = glm::normalize((vertices[i_1].position + vertices[i_2].position) / 2.0f);
			glm::vec3 p_5 = glm::normalize((vertices[i_2].position + vertices[i_0].position) / 2.0f);
			vertices.push_back({ p_3 / 2.0f, p_3 });
			int i_3 = vertices.size() - 1;
			vertices.push_back({ p_4 / 2.0f, p_4 });
			int i_4 = vertices.size() - 1;
			vertices.push_back({ p_5 / 2.0f, p_5 });
			int i_5 = vertices.size() - 1;

			indicies[i * 3] = i_3;
			indicies[i * 3 + 1] = i_4;
			indicies[i * 3 + 2] = i_5;

			indicies.push_back(i_0);
			indicies.push_back(i_3);
			indicies.push_back(i_5);

			indicies.push_back(i_1);
			indicies.push_back(i_4);
			indicies.push_back(i_3);

			indicies.push_back(i_2);
			indicies.push_back(i_5);
			indicies.push_back(i_4);
		}
	}

}
