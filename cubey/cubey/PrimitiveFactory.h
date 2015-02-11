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

		static Mesh* FullScreenQuad() {
			static Mesh* mesh = Mesh::Create(FullScreenQuadVerts(), GL_TRIANGLE_STRIP);
			return mesh;
		}
	};
}


