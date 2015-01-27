#include "ParticleTest.h"

#include "glm/gtc/random.hpp"

namespace cubey {


	void ParticleTest::Init() {
		//glEnable(GL_DEPTH_TEST);
		//glEnable(GL_CULL_FACE);
		//glClearColor(0.4, 0.4, 0.4, 1);

		srand(time(0));

		update_prog = new ShaderProgram();
		update_prog->AddShader(GL_COMPUTE_SHADER, "shaders\\particle_update_shader.glsl");
		update_prog->Link();

		render_prog = new ShaderProgram();
		render_prog->AddShader(GL_VERTEX_SHADER, "shaders\\particle_render_shader.glsl", "#define _VERTEX_S_");
		render_prog->AddShader(GL_FRAGMENT_SHADER, "shaders\\particle_render_shader.glsl", "#define _FRAGMENT_S_");
		render_prog->Link();

		glm::vec4* particle_data = new glm::vec4[PARTICLE_COUNT * 2];
		for (int i = 0; i < PARTICLE_COUNT; i++) {
			particle_data[i * 2] = glm::vec4(glm::ballRand(INITAL_RADIUS), glm::linearRand(0.0f, LIFE_SPAN));
			particle_data[i * 2 + 1] = glm::vec4(glm::ballRand(INITAL_VELOCITY), glm::linearRand(1.0f, 2.0f));
		}

		glGenBuffers(1, &ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(glm::vec4)*PARTICLE_COUNT*2, particle_data, GL_DYNAMIC_COPY);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);

		delete[] particle_data;

		for (int i = 0; i < ATTRACTOR_COUNT; i++) {
			attractor_pos.push_back(glm::vec4(glm::ballRand(ATTRACTOR_RADIUS), glm::linearRand(ATTRACTOR_MASS_MIN, ATTRACTOR_MASS_MAX)));
			attractor_v.push_back(glm::ballRand(ATTRACTOR_VELOCITY));
		}

		update_prog->Activate();
		update_prog->SetUniform("lifespan", LIFE_SPAN);

		render_prog->Activate();
		render_prog->SetUniform("lifespan", LIFE_SPAN);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	}

	void ParticleTest::Update(float delta_time) {

		for (int i = 0; i < ATTRACTOR_COUNT; i++) {
			attractor_pos[i].x += delta_time * attractor_v[i].x;
			attractor_pos[i].y += delta_time * attractor_v[i].y;
			attractor_pos[i].z += delta_time * attractor_v[i].z;

			glm::vec3 dist = - glm::vec3(attractor_pos[i]);
			attractor_v[i] += delta_time * glm::normalize(dist) / glm::dot(dist, dist);
		}

		update_prog->Activate();
		update_prog->SetUniform("delta_time", delta_time);
		update_prog->SetUniform("attractors", attractor_pos);
		//glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glDispatchCompute(PARTICLE_COUNT / 128, 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		//glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	void ParticleTest::Render() {
		render_prog->Activate();
		render_prog->SetUniform("u_mvp_mat", Camera::Main()->CalculateMVPMat(glm::mat4()));
		glBindBuffer(GL_ARRAY_BUFFER, ssbo);
		glVertexAttribPointer(0, 4, GL_FLOAT, false, 2 * sizeof(glm::vec4), (GLvoid*)0);
		glEnableVertexAttribArray(0);

		glDrawArrays(GL_POINTS, 0, PARTICLE_COUNT);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	

}
