#pragma once

#include "Vertex.h"

namespace cubey {
	class PrimitiveFactory
	{
	public:
		static std::vector<Vertex2<glm::vec3, glm::vec3>> UnitBoxUnlit();
		static std::vector<Vertex3<glm::vec3, glm::vec3, glm::vec3>> UnitBox();
		static std::vector<Vertex3<glm::vec3, glm::vec3, glm::vec3>> AxisIndicator();
	};
}


