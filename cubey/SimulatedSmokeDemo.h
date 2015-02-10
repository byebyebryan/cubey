#pragma once
#include "cubey.h"

namespace cubey {
	
	struct Slab {
		GLuint ping;
		GLuint pong;

		void Swap() {
			GLuint t = ping;
			ping = pong;
			pong = t;
		}
	};

	class SimulatedSmokeDemo : public IEngineEvents {
	public:
		void Init() override;
		void Update(float delta_time) override;
		void Render() override;

	private:
		void GenTexture(GLuint& tex, GLenum internal_format);
		void GenTexture(Slab& slab, GLenum internal_format);

		void FillObstacle();
		void Advert(float delta_time);
		void AddImpulse(float delta_time);
		void ApplyBuoyancy(float delta_time);
		void ApplyVorticityConfinement(float delta_time);
		void ComputeDivergence();
		void Jacobi();
		void Projection();

		Slab i_velocity;
		Slab i_temperature;
		Slab i_density;
		Slab i_pressure;
		GLuint i_divergence;
		GLuint i_obstacle;
		GLuint i_vorticity;
		GLuint i_phi_n_1_hat;
		GLuint i_phi_n_hat;
		GLuint i_shadow;

		ShaderProgram* init_fill_rgba_;
		ShaderProgram* init_fill_r_;
		ShaderProgram* init_fill_obstacle_;
		ShaderProgram* update_advect_rgba_;
		ShaderProgram* update_advect_r_;
		ShaderProgram* update_advect_r_mac_cormack_;
		ShaderProgram* update_splat_;
		ShaderProgram* update_buoyancy_;
		ShaderProgram* update_vorticity_;
		ShaderProgram* update_confinement_;
		ShaderProgram* update_divergence_;
		ShaderProgram* update_jacobi_;
		ShaderProgram* update_gradient_;

		ShaderProgram* render_shadow_;
		ShaderProgram* render_;

		GLuint fullscreen_quad_vao;

		glm::vec3 obsticle_position_;
		float obsticle_radius_;

		float velocity_dissipation_;
		float temperature_dissipation_;
		float temperature_decay_;
		float density_dissipation_;
		float density_decay_;

		glm::vec3 injection_location_;

		float temperature_injection_radius_;
		float temperature_injection_intensity_;

		float density_injection_radius_;
		float density_injection_intensity_;

		float ambient_temperature_;
		float buoyancy_;
		float weight_;

		float vorticity_strength_;
	};

}



