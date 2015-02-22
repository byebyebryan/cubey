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
		int num_triangles = indicies.size() / 3;
		for (int i = 0; i < num_triangles; i++) {
			int i_0 = indicies[i * 3];
			int i_1 = indicies[i * 3 + 1];
			int i_2 = indicies[i * 3 + 2];
			glm::vec3 p = (vertices[i_0].position + vertices[i_1].position + vertices[i_2].position) / 3.0f;
			vertices.push_back({ glm::normalize(p)/2.0f, glm::normalize(p) });
			int i_3 = vertices.size() - 1;
			indicies[i_2] = i_3;
			indicies.push_back(i_1);
			indicies.push_back(i_2);
			indicies.push_back(i_3);
			indicies.push_back(i_2);
			indicies.push_back(i_0);
			indicies.push_back(i_3);
		}
	}

}
