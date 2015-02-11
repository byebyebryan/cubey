#pragma once

#include "cubey.h"

namespace cubey {
	class MarchingCubesDemo : public IEngineEvents {
	public:
		void Init() override;
		void Update(float delta_time) override;
		void Render() override;

	private:
		ShaderProgram* cubes_render_prog_;
		GLuint ssbo_;
		Mesh* mesh_;
		MeshInstance* mesh_instance_;
	};
}