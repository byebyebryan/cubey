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

	class SmokeDemo : public EngineEventsBase {
	public:
		void Init() override;
		void Update(float delta_time) override;
		void Render() override;

	private:
		void GenTexture(GLuint& tex, GLenum internal_format);
		void GenTexture(Slab& slab, GLenum internal_format);

		void FillObstacle(float delta_time);
		void Advert(float delta_time);
		void AddImpulse(float delta_time);
		void AddExplosion(float delta_time);
		void ApplyBuoyancy(float delta_time);
		void ApplyVorticityConfinement(float delta_time);
		void ComputeDivergence();
		void Jacobi();
		void Projection();

		void Blur();

		std::vector<float> ComputeGaussianKernel(float sigma);

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

		GLuint i_obstacle_v;

		Slab i_density_blured;
		Slab i_shadow_blured;
		Slab i_temperature_blured;
		Slab i_obstacle_blured;

		ShaderProgram* init_fill_rgba_;
		ShaderProgram* init_fill_r_;
		ShaderProgram* init_fill_obstacle_;
		ShaderProgram* update_advect_rgba_;
		ShaderProgram* update_advect_r_;
		ShaderProgram* update_advect_r_mac_cormack_;
		ShaderProgram* update_splat_;
		ShaderProgram* update_explosion_;
		ShaderProgram* update_buoyancy_;
		ShaderProgram* update_vorticity_;
		ShaderProgram* update_confinement_;
		ShaderProgram* update_divergence_;
		ShaderProgram* update_jacobi_;
		ShaderProgram* update_gradient_;

		ShaderProgram* render_blur_;
		ShaderProgram* render_shadow_;
		ShaderProgram* render_;

		MeshInstance* fullscreen_quad_;

		glm::vec3 obsticle_position_;
		float obsticle_radius_;

		float velocity_dissipation_;
		float temperature_dissipation_;
		float temperature_decay_;
		float density_dissipation_;
		float density_decay_;

		bool enable_injection_;

		glm::vec3 injection_location_;

		float temperature_injection_radius_;
		float temperature_injection_intensity_;

		float density_injection_radius_;
		float density_injection_intensity_;

		bool add_explosion_;
		float explosion_timer_;
		float explosion_concetration_;
		float explosion_force_min_;
		float explosion_force_max_;
		float explosion_injection_ratio_;
		float explosion_temperature_ratio_;

		bool enable_buoyancy_;
		float ambient_temperature_;
		float buoyancy_;
		float weight_;

		float vorticity_strength_;

		bool camera_rotation_;

		int jacobi_iterations_;
		bool simulation_paused_;

		bool blur_density_;
		float density_blur_sigma_;
		float density_sample_jittering_;
		bool blur_shadow_;
		float shadow_blur_sigma_;
		float shadow_sample_jittering_;

		float color_absorption_;
		float light_absorption_;
		float light_intensity_;
		float ambient_light_;

		glm::vec3 light_color_;
		glm::vec3 smoke_color_;

		bool enable_shadows_;

		bool enable_temperature_color_;

		bool enable_obstacle_motion_;
		bool enable_obstacle_motion_prev_;
		glm::vec3 obstacle_current_pos_;
		glm::vec3 obstacle_current_v_;
	};

}



