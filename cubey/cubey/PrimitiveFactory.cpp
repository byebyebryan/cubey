#include "PrimitiveFactory.h"

#include "glm/gtx/transform.hpp"

namespace cubey {
	const glm::vec3 red = { 1.0f, 0.0f, 0.0f };
	const glm::vec3 green = { 0.0f, 1.0f, 0.0f };
	const glm::vec3 blue = { 0.0f, 0.0f, 1.0f };

	const glm::vec3 up = { 1.0f, 0.0f, 0.0f };
	const glm::vec3 right = { 0.0f, 1.0f, 0.0f };
	const glm::vec3 forward = { 0.0f, 0.0f, 1.0f };

	std::vector<Vertex2<glm::vec3, glm::vec3>> PrimitiveFactory::UnitBoxUnlit() {
		std::vector<Vertex2<glm::vec3, glm::vec3>> box_verts = {
			{ { -0.5f, -0.5f, -0.5f }, blue},
			{ { 0.5f, -0.5f, -0.5f }, blue},
			{ { -0.5f, 0.5f, -0.5f }, blue},

			{ { 0.5f, -0.5f, -0.5f }, blue},
			{ { 0.5f, 0.5f, -0.5f }, blue},
			{ { -0.5f, 0.5f, -0.5f }, blue},

			{ { 0.5f, -0.5f, 0.5f }, blue},
			{ { -0.5f, -0.5f, 0.5f }, blue},
			{ { -0.5f, 0.5f, 0.5f }, blue},

			{ { 0.5f, 0.5f, 0.5f }, blue},
			{ { 0.5f, -0.5f, 0.5f }, blue},
			{ { -0.5f, 0.5f, 0.5f }, blue},

			{ { 0.5f, -0.5f, -0.5f }, green},
			{ { -0.5f, -0.5f, -0.5f }, green},
			{ { -0.5f, -0.5f, 0.5f }, green},

			{ { 0.5f, -0.5f, 0.5f }, green},
			{ { 0.5f, -0.5f, -0.5f }, green},
			{ { -0.5f, -0.5f, 0.5f }, green},

			{ { -0.5f, 0.5f, -0.5f }, green},
			{ { 0.5f, 0.5f, -0.5f }, green},
			{ { -0.5f, 0.5f, 0.5f }, green},

			{ { 0.5f, 0.5f, -0.5f }, green},
			{ { 0.5f, 0.5f, 0.5f }, green},
			{ { -0.5f, 0.5f, 0.5f }, green},

			{ { -0.5f, -0.5f, 0.5f }, red},
			{ { -0.5f, -0.5f, -0.5f }, red},
			{ { -0.5f, 0.5f, -0.5f }, red},

			{ { -0.5f, 0.5f, 0.5f }, red},
			{ { -0.5f, -0.5f, 0.5f }, red},
			{ { -0.5f, 0.5f, -0.5f }, red},

			{ { 0.5f, -0.5f, -0.5f }, red},
			{ { 0.5f, -0.5f, 0.5f }, red},
			{ { 0.5f, 0.5f, -0.5f }, red},

			{ { 0.5f, -0.5f, 0.5f }, red},
			{ { 0.5f, 0.5f, 0.5f }, red},
			{ { 0.5f, 0.5f, -0.5f }, red},
		};
		return box_verts;
	}

	std::vector<Vertex3<glm::vec3, glm::vec3, glm::vec3>> PrimitiveFactory::UnitBox() {
		std::vector<Vertex3<glm::vec3, glm::vec3, glm::vec3>> box_verts = {
			{ { -0.5f, -0.5f, -0.5f }, blue, -forward },
			{ { 0.5f, -0.5f, -0.5f }, blue, -forward },
			{ { -0.5f, 0.5f, -0.5f }, blue, -forward },

			{ { 0.5f, -0.5f, -0.5f }, blue, -forward },
			{ { 0.5f, 0.5f, -0.5f }, blue, -forward },
			{ { -0.5f, 0.5f, -0.5f }, blue, -forward },

			{ { 0.5f, -0.5f, 0.5f }, blue, forward },
			{ { -0.5f, -0.5f, 0.5f }, blue, forward },
			{ { -0.5f, 0.5f, 0.5f }, blue, forward },

			{ { 0.5f, 0.5f, 0.5f }, blue, forward },
			{ { 0.5f, -0.5f, 0.5f }, blue, forward },
			{ { -0.5f, 0.5f, 0.5f }, blue, forward },

			{ { 0.5f, -0.5f, -0.5f }, green, -up },
			{ { -0.5f, -0.5f, -0.5f }, green, -up },
			{ { -0.5f, -0.5f, 0.5f }, green, -up },

			{ { 0.5f, -0.5f, 0.5f }, green, -up },
			{ { 0.5f, -0.5f, -0.5f }, green, -up },
			{ { -0.5f, -0.5f, 0.5f }, green, -up },

			{ { -0.5f, 0.5f, -0.5f }, green, up },
			{ { 0.5f, 0.5f, -0.5f }, green, up },
			{ { -0.5f, 0.5f, 0.5f }, green, up },

			{ { 0.5f, 0.5f, -0.5f }, green, up },
			{ { 0.5f, 0.5f, 0.5f }, green, up },
			{ { -0.5f, 0.5f, 0.5f }, green, up },

			{ { -0.5f, -0.5f, 0.5f }, red, -right },
			{ { -0.5f, -0.5f, -0.5f }, red, -right },
			{ { -0.5f, 0.5f, -0.5f }, red, -right },

			{ { -0.5f, 0.5f, 0.5f }, red, -right },
			{ { -0.5f, -0.5f, 0.5f }, red, -right },
			{ { -0.5f, 0.5f, -0.5f }, red, -right },

			{ { 0.5f, -0.5f, -0.5f }, red, right },
			{ { 0.5f, -0.5f, 0.5f }, red, right },
			{ { 0.5f, 0.5f, -0.5f }, red, right },

			{ { 0.5f, -0.5f, 0.5f }, red, right },
			{ { 0.5f, 0.5f, 0.5f }, red, right },
			{ { 0.5f, 0.5f, -0.5f }, red, right },
		};
		return box_verts;
	}

	std::vector<Vertex3<glm::vec3, glm::vec3, glm::vec3>> PrimitiveFactory::AxisIndicator() {
		auto vertex_array_0 = PrimitiveFactory::UnitBox();
		VertexArrayHelper::ChangeColor(vertex_array_0, vertex_array_0[0].attrib_ptr_1(), glm::vec3(1.0f, 0.0f, 0.0f));
		VertexArrayHelper::ApplyTransform(vertex_array_0, vertex_array_0[0].attrib_ptr_0(), vertex_array_0[0].attrib_ptr_2(),
			glm::translate(glm::vec3(1.1f, 0.0f, 0.0f)) * glm::scale(glm::vec3(2.0f, 0.1f, 0.1f)));

		auto vertex_array_1 = PrimitiveFactory::UnitBox();
		VertexArrayHelper::ChangeColor(vertex_array_1, vertex_array_1[0].attrib_ptr_1(), glm::vec3(0.0f, 1.0f, 0.0f));
		VertexArrayHelper::ApplyTransform(vertex_array_1, vertex_array_1[0].attrib_ptr_0(), vertex_array_1[0].attrib_ptr_2(),
			glm::translate(glm::vec3(0.0f, 1.1f, 0.0f)) * glm::scale(glm::vec3(0.1f, 2.0f, 0.1f)));

		auto vertex_array_2 = PrimitiveFactory::UnitBox();
		VertexArrayHelper::ChangeColor(vertex_array_2, vertex_array_2[0].attrib_ptr_1(), glm::vec3(0.0f, 0.0f, 1.0f));
		VertexArrayHelper::ApplyTransform(vertex_array_2, vertex_array_2[0].attrib_ptr_0(), vertex_array_2[0].attrib_ptr_2(),
			glm::translate(glm::vec3(0.0f, 0.0f, 1.1f)) * glm::scale(glm::vec3(0.1f, 0.1f, 2.0f)));

		auto combined_array = VertexArrayHelper::CombineArrays(vertex_array_0, vertex_array_1);
		combined_array = VertexArrayHelper::CombineArrays(combined_array, vertex_array_2);

		return combined_array;
	}

	

}
