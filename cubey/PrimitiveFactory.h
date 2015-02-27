#pragma once

#include "VertexArray.h"
#include "Mesh.h"

namespace cubey {
	class PrimitiveFactory
	{
	public:
		static MeshIndexed* UnitXZPlane();
		static MeshIndexed* UnitCube();
		static MeshIndexed* UnitSphere(int iterations = 5);

		static Mesh* FullScreenQuad();

	private:
		static void CircularizationIterate(Vertex<VPosition3, VNormal>::Array& vertices, std::vector<unsigned int>& indicies);
	};
}


