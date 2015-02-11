#include "PrimitiveFactory.h"

#include "glm/gtx/transform.hpp"

namespace cubey {

	const glm::vec3 kUp = { 1.0f, 0.0f, 0.0f };
	const glm::vec3 kRight = { 0.0f, 1.0f, 0.0f };
	const glm::vec3 kForward = { 0.0f, 0.0f, 1.0f };

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
			{ { -0.5f, -0.5f, -0.5f }, -kForward },
			{ { 0.5f, -0.5f, -0.5f }, -kForward },
			{ { -0.5f, 0.5f, -0.5f }, -kForward },

			{ { 0.5f, -0.5f, -0.5f }, -kForward },
			{ { 0.5f, 0.5f, -0.5f }, -kForward },
			{ { -0.5f, 0.5f, -0.5f }, -kForward },

			{ { 0.5f, -0.5f, 0.5f }, kForward },
			{ { -0.5f, -0.5f, 0.5f }, kForward },
			{ { -0.5f, 0.5f, 0.5f }, kForward },

			{ { 0.5f, 0.5f, 0.5f }, kForward },
			{ { 0.5f, -0.5f, 0.5f }, kForward },
			{ { -0.5f, 0.5f, 0.5f }, kForward },

			{ { 0.5f, -0.5f, -0.5f }, -kUp },
			{ { -0.5f, -0.5f, -0.5f }, -kUp },
			{ { -0.5f, -0.5f, 0.5f }, -kUp },

			{ { 0.5f, -0.5f, 0.5f }, -kUp },
			{ { 0.5f, -0.5f, -0.5f }, -kUp },
			{ { -0.5f, -0.5f, 0.5f }, -kUp },

			{ { -0.5f, 0.5f, -0.5f }, kUp },
			{ { 0.5f, 0.5f, -0.5f }, kUp },
			{ { -0.5f, 0.5f, 0.5f }, kUp },

			{ { 0.5f, 0.5f, -0.5f }, kUp },
			{ { 0.5f, 0.5f, 0.5f }, kUp },
			{ { -0.5f, 0.5f, 0.5f }, kUp },

			{ { -0.5f, -0.5f, 0.5f }, -kRight },
			{ { -0.5f, -0.5f, -0.5f }, -kRight },
			{ { -0.5f, 0.5f, -0.5f }, -kRight },

			{ { -0.5f, 0.5f, 0.5f }, -kRight },
			{ { -0.5f, -0.5f, 0.5f }, -kRight },
			{ { -0.5f, 0.5f, -0.5f }, -kRight },

			{ { 0.5f, -0.5f, -0.5f }, kRight },
			{ { 0.5f, -0.5f, 0.5f }, kRight },
			{ { 0.5f, 0.5f, -0.5f }, kRight },

			{ { 0.5f, -0.5f, 0.5f }, kRight },
			{ { 0.5f, 0.5f, 0.5f }, kRight },
			{ { 0.5f, 0.5f, -0.5f }, kRight },
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

}
