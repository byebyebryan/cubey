#include "PrimitiveFactory.h"

#include "glm/gtx/transform.hpp"

namespace cubey {

	Vertex<VPosition3>::Array PrimitiveFactory::UnitBoxVerts() {
		static Vertex<VPosition3>::Array box_verts = {
			{ { -0.5f, -0.5f, -0.5f }},
			{ { 0.5f, -0.5f, -0.5f }},
			{ { -0.5f, 0.5f, -0.5f }},

			{ { 0.5f, -0.5f, -0.5f }},
			{ { 0.5f, 0.5f, -0.5f }},
			{ { -0.5f, 0.5f, -0.5f }},

			{ { 0.5f, -0.5f, 0.5f }},
			{ { -0.5f, -0.5f, 0.5f }},
			{ { -0.5f, 0.5f, 0.5f }},

			{ { 0.5f, 0.5f, 0.5f }},
			{ { 0.5f, -0.5f, 0.5f }},
			{ { -0.5f, 0.5f, 0.5f }},

			{ { 0.5f, -0.5f, -0.5f }},
			{ { -0.5f, -0.5f, -0.5f }},
			{ { -0.5f, -0.5f, 0.5f }},

			{ { 0.5f, -0.5f, 0.5f }},
			{ { 0.5f, -0.5f, -0.5f }},
			{ { -0.5f, -0.5f, 0.5f }},

			{ { -0.5f, 0.5f, -0.5f }},
			{ { 0.5f, 0.5f, -0.5f }},
			{ { -0.5f, 0.5f, 0.5f }},

			{ { 0.5f, 0.5f, -0.5f }},
			{ { 0.5f, 0.5f, 0.5f }},
			{ { -0.5f, 0.5f, 0.5f }},

			{ { -0.5f, -0.5f, 0.5f }},
			{ { -0.5f, -0.5f, -0.5f }},
			{ { -0.5f, 0.5f, -0.5f }},

			{ { -0.5f, 0.5f, 0.5f }},
			{ { -0.5f, -0.5f, 0.5f }},
			{ { -0.5f, 0.5f, -0.5f }},

			{ { 0.5f, -0.5f, -0.5f }},
			{ { 0.5f, -0.5f, 0.5f }},
			{ { 0.5f, 0.5f, -0.5f }},

			{ { 0.5f, -0.5f, 0.5f }},
			{ { 0.5f, 0.5f, 0.5f }},
			{ { 0.5f, 0.5f, -0.5f }},
		};
		return box_verts;
	}

	Vertex<VPosition3, VNormal>::Array PrimitiveFactory::UnitBoxWNormalVerts() {
		static Vertex<VPosition3, VNormal>::Array box_verts = {
			{ { -0.5f, -0.5f, -0.5f }, {0.0f, 0.0f, -1.0f} },
			{ { 0.5f, -0.5f, -0.5f }, {0.0f, 0.0f, -1.0f} },
			{ { -0.5f, 0.5f, -0.5f }, {0.0f, 0.0f, -1.0f} },

			{ { 0.5f, -0.5f, -0.5f }, {0.0f, 0.0f, -1.0f} },
			{ { 0.5f, 0.5f, -0.5f }, {0.0f, 0.0f, -1.0f} },
			{ { -0.5f, 0.5f, -0.5f }, {0.0f, 0.0f, -1.0f} },

			{ { 0.5f, -0.5f, 0.5f }, {0.0f, 0.0f, 1.0f} },
			{ { -0.5f, -0.5f, 0.5f }, {0.0f, 0.0f, 1.0f} },
			{ { -0.5f, 0.5f, 0.5f }, {0.0f, 0.0f, 1.0f} },

			{ { 0.5f, 0.5f, 0.5f }, {0.0f, 0.0f, 1.0f} },
			{ { 0.5f, -0.5f, 0.5f }, {0.0f, 0.0f, 1.0f} },
			{ { -0.5f, 0.5f, 0.5f }, {0.0f, 0.0f, 1.0f} },

			{ { 0.5f, -0.5f, -0.5f }, {0.0f, -1.0f, 0.0f} },
			{ { -0.5f, -0.5f, -0.5f }, {0.0f, -1.0f, 0.0f} },
			{ { -0.5f, -0.5f, 0.5f }, {0.0f, -1.0f, 0.0f} },

			{ { 0.5f, -0.5f, 0.5f }, {0.0f, -1.0f, 0.0f} },
			{ { 0.5f, -0.5f, -0.5f }, {0.0f, -1.0f, 0.0f} },
			{ { -0.5f, -0.5f, 0.5f }, {0.0f, -1.0f, 0.0f} },

			{ { -0.5f, 0.5f, -0.5f }, {0.0f, 1.0f, 0.0f} },
			{ { 0.5f, 0.5f, -0.5f }, {0.0f, 1.0f, 0.0f} },
			{ { -0.5f, 0.5f, 0.5f }, {0.0f, 1.0f, 0.0f} },

			{ { 0.5f, 0.5f, -0.5f }, {0.0f, 1.0f, 0.0f} },
			{ { 0.5f, 0.5f, 0.5f }, {0.0f, 1.0f, 0.0f} },
			{ { -0.5f, 0.5f, 0.5f }, {0.0f, 1.0f, 0.0f} },

			{ { -0.5f, -0.5f, 0.5f }, {-1.0f, 0.0f, 0.0f} },
			{ { -0.5f, -0.5f, -0.5f }, {-1.0f, 0.0f, 0.0f} },
			{ { -0.5f, 0.5f, -0.5f }, {-1.0f, 0.0f, 0.0f} },

			{ { -0.5f, 0.5f, 0.5f }, {-1.0f, 0.0f, 0.0f} },
			{ { -0.5f, -0.5f, 0.5f }, {-1.0f, 0.0f, 0.0f} },
			{ { -0.5f, 0.5f, -0.5f }, {-1.0f, 0.0f, 0.0f} },

			{ { 0.5f, -0.5f, -0.5f }, {1.0f, 0.0f, 0.0f} },
			{ { 0.5f, -0.5f, 0.5f }, {1.0f, 0.0f, 0.0f} },
			{ { 0.5f, 0.5f, -0.5f }, {1.0f, 0.0f, 0.0f} },

			{ { 0.5f, -0.5f, 0.5f }, {1.0f, 0.0f, 0.0f} },
			{ { 0.5f, 0.5f, 0.5f }, {1.0f, 0.0f, 0.0f} },
			{ { 0.5f, 0.5f, -0.5f }, {1.0f, 0.0f, 0.0f} },
		};
		return box_verts;
	}

	Vertex<VPosition2, VTexCoord2>::Array PrimitiveFactory::FullScreenQuadVerts() {
		static Vertex<VPosition2, VTexCoord2>::Array verts = {
			{ { -1.0f, 1.0f }, { 0.0f, 1.0f } },
			{ { -1.0f, -1.0f }, { 0.0f, 0.0f } },
			{ { 1.0f, 1.0f }, { 1.0f, 1.0f } },
			{ { 1.0f, -1.0f }, { 1.0f, 0.0f } },
		};
		return verts;
	}

	Vertex<VPosition3, VNormal>::Array PrimitiveFactory::UnitTetrahedronWNormalVerts() {
		static float d = glm::radians(120.0f);
		static Vertex<VPosition3, VNormal>::Array verts = {
			{ { 0.0f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ glm::rotateX(glm::vec3(0.0f, 0.5f, 0.0f), d), glm::rotateX(glm::vec3(0.0f, 1.0f, 0.0f), d) },
			{ glm::rotateY(glm::rotateX(glm::vec3(0.0f, 0.5f, 0.0f), d), d), glm::rotateY(glm::rotateX(glm::vec3(0.0f, 1.0f, 0.0f), d), d) },
			{ glm::rotateY(glm::rotateX(glm::vec3(0.0f, 0.5f, 0.0f), d), d*2.0f), glm::rotateY(glm::rotateX(glm::vec3(0.0f, 1.0f, 0.0f), d), d*2.0f) },
		};
		return verts;
	}

	void PrimitiveFactory::UnitBallExpand(Vertex<VPosition3, VNormal>::Array& vertices, std::vector<unsigned int>& indicies) {
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
			vertices.push_back({ p_3/2.0f, p_3 });
			int i_3 = vertices.size() - 1;
			vertices.push_back({ p_4 / 2.0f, p_4 });
			int i_4 = vertices.size() - 1;
			vertices.push_back({ p_5 / 2.0f, p_5 });
			int i_5 = vertices.size() - 1;

			indicies[i *3] = i_3;
			indicies[i *3 + 1] = i_4;
			indicies[i*3+2] = i_5;

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
