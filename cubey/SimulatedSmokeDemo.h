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

		Slab i_velocity;
		Slab i_temperature;
		Slab i_density;
		Slab i_pressure;
		GLuint i_divergence;
		GLuint i_obstacle;

		GLuint i_vorticity;

		GLuint i_phi_n_1_hat;
		GLuint i_phi_n_hat;

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

		ShaderProgram* init_fill_verts_;

		ShaderProgram* render_;

		ShaderProgram* render_first_pass_;
		ShaderProgram* render_second_pass_;

		ShaderProgram* render_debug_;

		Mesh* mesh;
		SimpleMeshInstance* mesh_instance;

		GLuint fullscreen_quad_vao;

		GLuint t_entry_points;
		GLuint t_exit_points;
		GLuint fb_entry_points;
		GLuint fb_exit_points;

		GLuint ssbo_;
	};

}



