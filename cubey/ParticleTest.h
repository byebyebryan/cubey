#pragma once

#include "cubey.h"

#define PARTICLE_COUNT 51200
#define LIFE_SPAN 2.5f
#define INITAL_RADIUS 2.5f
#define INITAL_VELOCITY 2.0f

#define ATTRACTOR_COUNT 16
#define ATTRACTOR_RADIUS 5.0f
#define ATTRACTOR_VELOCITY 1.0f
#define ATTRACTOR_MASS_MIN 100.0f
#define ATTRACTOR_MASS_MAX 500.0f

namespace cubey {
	class ParticleTest : public IEngineEvents {
	public:
		void Init() override;
		void Update(float delta_time) override;
		void Render() override;

	private:
		ShaderProgram* update_prog;
		ShaderProgram* render_prog;

		GLuint ssbo;

		std::vector<glm::vec4> attractor_pos;
		std::vector<glm::vec3> attractor_v;
	};
}


