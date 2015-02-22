#pragma once

#include "VertexArray.h"
#include "Mesh.h"

namespace cubey {
	class PrimitiveFactory
	{
	public:
		static Vertex<VPosition3>::Array UnitBoxVerts();
		static Vertex<VPosition3, VNormal>::Array UnitBoxWNormalVerts();
		static Vertex<VPosition2, VTexCoord2>::Array FullScreenQuadVerts();

		static Vertex<VPosition3, VNormal>::Array UnitTetrahedronWNormalVerts();

		static Mesh* UnitBox() {
			static Mesh* mesh = Mesh::Create(UnitBoxVerts(), GL_TRIANGLES);
			return mesh;
		}

		template<typename T>
		static Mesh* UnitBox(const T& color) {
			auto verts = VertexArrayHelper::AppendAttrib(UnitBoxVerts(), color);
			Mesh* mesh = Mesh::Create(verts, GL_TRIANGLES);
			return mesh;
		}

		template<typename T>
		static Mesh* UnitBoxWNormal(const T& color) {
			auto verts = VertexArrayHelper::AppendAttrib(UnitBoxWNormalVerts(), color);
			Mesh* mesh = Mesh::Create(verts, GL_TRIANGLES);
			return mesh;
		}

		template<typename T>
		static MeshIndexed* UnitBallWNormal(const T& color, int iterations = 5) {
			Vertex<VPosition3, VNormal>::Array vertices = UnitTetrahedronWNormalVerts();
			std::vector<unsigned int> indicies = { 0, 2, 1, 0, 3, 2, 0, 1, 3, 1, 2, 3 };
			for (int i = 0; i < iterations; i++) {
				UnitBallExpand(vertices, indicies);
			}
			auto cverts = VertexArrayHelper::AppendAttrib(vertices, color);
			MeshIndexed* mesh = MeshIndexed::Create(cverts, indicies, GL_TRIANGLES);
			return mesh;
		}

		static Mesh* FullScreenQuad() {
			static Mesh* mesh = Mesh::Create(FullScreenQuadVerts(), GL_TRIANGLE_STRIP);
			return mesh;
		}

	private:
		static void UnitBallExpand(Vertex<VPosition3, VNormal>::Array& vertices, std::vector<unsigned int>& indicies);
	};
}


