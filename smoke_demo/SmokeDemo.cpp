#include "SmokeDemo.h"

#include "glm/gtc/random.hpp"

#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

#define GLOBAL_SIZE_X 96
#define GLOBAL_SIZE_Y 96
#define GLOBAL_SIZE_Z 96

#define WORKGROUP_COUNT_X GLOBAL_SIZE_X/LOCAL_WORKGROUP_SIZE_X
#define WORKGROUP_COUNT_Y GLOBAL_SIZE_Y/LOCAL_WORKGROUP_SIZE_Y
#define WORKGROUP_COUNT_Z GLOBAL_SIZE_Z/LOCAL_WORKGROUP_SIZE_Z

namespace cubey {

	void SmokeDemo::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_TEXTURE_3D);
		glClearColor(0.5, 0.5, 0.5, 1.0);

		ShaderManager::BufferCleaner shader_cleaner;

		init_fill_rgba_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_FILL_RGBA__");
		init_fill_r_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_FILL_R__");
		init_fill_obstacle_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_FILL_OBSTACLE__");
		update_advect_rgba_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_ADVECT_RGBA__");
		update_advect_r_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_ADVECT_R__");
		update_advect_r_mac_cormack_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_ADVECT_R_MAC_CORMACK__");
		update_splat_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_SPLAT__");
		update_explosion_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_EXPLOSION__");
		update_buoyancy_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_BUOYANCY__");
		update_vorticity_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_VORTICITY__");
		update_confinement_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_CONFINEMENT__");
		update_divergence_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_DIVERGENCE__");
		update_jacobi_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_JACOBI__");
		update_gradient_ = ShaderManager::Main()->CreateProgram("smoke_compute.__CS_PROJECTION__");
		render_blur_ = ShaderManager::Main()->CreateProgram("smoke_render.__CS_GAUSSIAN_BLUR__");
		render_shadow_ = ShaderManager::Main()->CreateProgram("smoke_render.__CS_SHADOW__");
		render_ = ShaderManager::Main()->CreateProgram("smoke_render.VS.FS");

		GenTexture(i_velocity, GL_RGBA16F);
		GenTexture(i_temperature, GL_R16F);
		GenTexture(i_density, GL_R16F);
		GenTexture(i_pressure, GL_R16F);
		GenTexture(i_divergence, GL_R16F);
		GenTexture(i_obstacle, GL_R16F);
		GenTexture(i_vorticity, GL_RGBA16F);
		GenTexture(i_obstacle_v, GL_RGBA16F);

		GenTexture(i_phi_n_1_hat, GL_R16F);
		GenTexture(i_phi_n_hat, GL_R16F);

		GenTexture(i_shadow, GL_R16F);

		GenTexture(i_density_blured, GL_R16F);
		GenTexture(i_shadow_blured, GL_R16F);
		GenTexture(i_temperature_blured, GL_R16F);
		GenTexture(i_obstacle_blured, GL_R16F);

		init_fill_rgba_->Activate();

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		init_fill_r_->Activate();

		glBindImageTexture(0, i_temperature.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		glBindImageTexture(0, i_density.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		fullscreen_quad_ = PrimitiveFactory::FullScreenQuad()->CreateInstance(render_);

		Camera::Main()->transform_.TranslateTo(glm::vec3(0, 0, -1.5f));

		Camera::Main()->mouse_wheel_speed_ = 1.0f;
		Camera::Main()->movement_speed_ = 1.0f;

		obsticle_position_ = glm::vec3(0.5f);
		obsticle_radius_ = 0.0f;
		enable_obstacle_motion_ = false;
		enable_obstacle_motion_prev_ = false;

		velocity_dissipation_ = -2.0f;
		temperature_dissipation_ = -2.0f;
		temperature_decay_ = -3.0f;
		density_dissipation_ = -2.0f;
		density_decay_ = -3.0f;

		enable_injection_ = true;
		injection_location_ = glm::vec3(0.5f, 0.1f, 0.5f);

		temperature_injection_radius_ = 0.5f;
		temperature_injection_intensity_ = 1.5f;

		density_injection_radius_ = 1.0f;
		density_injection_intensity_ = 1.0f;

		enable_buoyancy_ = true;
		ambient_temperature_ = 0.0f;
		buoyancy_ = 1.0f;
		weight_ = 1.0f;

		vorticity_strength_ = 10.0f;

		camera_rotation_ = true;

		jacobi_iterations_ = 20;
		simulation_paused_ = false;

		add_explosion_ = true;
		explosion_timer_ = 3.0f;
		explosion_concetration_ = 0.3f;
		explosion_force_min_ = 1.75f;
		explosion_force_max_ = 2.0f;
		explosion_injection_ratio_ = 0.0f;
		explosion_temperature_ratio_ = 0.0f;

		blur_density_ = true;
		density_blur_sigma_ = 0.5f;
		density_sample_jittering_ = 1.0f;
		blur_shadow_ = true;
		shadow_blur_sigma_ = 1.0f;
		shadow_sample_jittering_ = 0.5f;

		color_absorption_ = 2.0f;
		light_absorption_ = 2.0f;
		light_intensity_ = 2.0f;
		ambient_light_ = 0.0f;

		light_color_ = glm::vec3(1.0f);
		smoke_color_ = glm::vec3(1.0f, 0.5f, 0.0f);

		enable_shadows_ = true;

		enable_temperature_color_ = false;

		TwAddVarRW(UI::Main()->tw_bar_, "rotating camera", TW_TYPE_BOOLCPP, &camera_rotation_, "group=Camera");

		TwAddVarRW(UI::Main()->tw_bar_, "obstacle position", TW_TYPE_DIR3F, &obsticle_position_, "group=Obstacle");
		TwAddVarRW(UI::Main()->tw_bar_, "obstacle radius", TW_TYPE_FLOAT, &obsticle_radius_, "min=0 max=32 step=2 group=Obstacle");
		TwAddVarRW(UI::Main()->tw_bar_, "enable obstacle motion", TW_TYPE_BOOLCPP, &enable_obstacle_motion_, "group=Obstacle");

		TwAddVarRW(UI::Main()->tw_bar_, "velocity dissipation log10", TW_TYPE_FLOAT, &velocity_dissipation_, "min=-5 max=-1 step=0.25 group=Advert");
		TwAddVarRW(UI::Main()->tw_bar_, "temperature dissipation log10", TW_TYPE_FLOAT, &temperature_dissipation_, "min=-5 max=-1 step=0.25 group=Advert");
		TwAddVarRW(UI::Main()->tw_bar_, "temperature decay log10", TW_TYPE_FLOAT, &temperature_decay_, "min=-5 max=0 step=0.25 group=Advert");
		TwAddVarRW(UI::Main()->tw_bar_, "density dissipation log10", TW_TYPE_FLOAT, &density_dissipation_, "min=-5 max=-1 step=0.25 group=Advert");
		TwAddVarRW(UI::Main()->tw_bar_, "density decay log10", TW_TYPE_FLOAT, &density_decay_, "min=-5 max=0 step=0.25 group=Advert");

		TwAddVarRW(UI::Main()->tw_bar_, "enable injection", TW_TYPE_BOOLCPP, &enable_injection_, "group=Injection");
		TwAddVarRW(UI::Main()->tw_bar_, "inject position", TW_TYPE_DIR3F, &injection_location_, "group=Injection");
		TwAddVarRW(UI::Main()->tw_bar_, "temperature inject concentration", TW_TYPE_FLOAT, &temperature_injection_radius_, "min=0.1 max=2.0 step=0.1 group=Injection");
		TwAddVarRW(UI::Main()->tw_bar_, "temperature inject intensity log10", TW_TYPE_FLOAT, &temperature_injection_intensity_, "min=0 max=4 step=0.25 group=Injection");
		TwAddVarRW(UI::Main()->tw_bar_, "density inject concentration", TW_TYPE_FLOAT, &density_injection_radius_, "min=0.1 max=2.0 step=0.1 group=Injection");
		TwAddVarRW(UI::Main()->tw_bar_, "density inject intensity log10", TW_TYPE_FLOAT, &density_injection_intensity_, "min=0 max=4 step=0.25 group=Injection");

		TwAddVarRW(UI::Main()->tw_bar_, "enable buoyancy", TW_TYPE_BOOLCPP, &enable_buoyancy_, "group=Buoyancy");
		TwAddVarRW(UI::Main()->tw_bar_, "ambient temperature", TW_TYPE_FLOAT, &ambient_temperature_, "min=-10 max=10 step=0.25 group=Buoyancy");
		TwAddVarRW(UI::Main()->tw_bar_, "buoyancy log10", TW_TYPE_FLOAT, &buoyancy_, "min=0 max=5 step=0.25 group=Buoyancy");
		TwAddVarRW(UI::Main()->tw_bar_, "weight log10", TW_TYPE_FLOAT, &weight_, "min=0 max=5 step=0.25 group=Buoyancy");

		TwAddVarRW(UI::Main()->tw_bar_, "vorticity strength", TW_TYPE_FLOAT, &vorticity_strength_, "min=0 max=100 step=5 group=Vorticity");

		TwAddVarRW(UI::Main()->tw_bar_, "add explosion", TW_TYPE_BOOLCPP, &add_explosion_, "group=Explosion");
		TwAddVarRW(UI::Main()->tw_bar_, "explosion timer", TW_TYPE_FLOAT, &explosion_timer_, "min=0.5 max=10 step=0.5 group=Explosion");
		TwAddVarRW(UI::Main()->tw_bar_, "explosion concentration", TW_TYPE_FLOAT, &explosion_concetration_, "min=0.1 max=2.0 step=0.1 group=Explosion");
		TwAddVarRW(UI::Main()->tw_bar_, "explosion force min log10", TW_TYPE_FLOAT, &explosion_force_min_, "min=-2 max=3 step=0.25 group=Explosion");
		TwAddVarRW(UI::Main()->tw_bar_, "explosion force max log10", TW_TYPE_FLOAT, &explosion_force_max_, "min=-2 max=3 step=0.25 group=Explosion");
		TwAddVarRW(UI::Main()->tw_bar_, "explosion temperature ratio log10", TW_TYPE_FLOAT, &explosion_temperature_ratio_, "min=-2 max=2 step=0.25 group=Explosion");
		TwAddVarRW(UI::Main()->tw_bar_, "explosion injection ratio log10", TW_TYPE_FLOAT, &explosion_injection_ratio_, "min=-2 max=2 step=0.25 group=Explosion");
		
		TwAddVarRW(UI::Main()->tw_bar_, "jacobi iterations", TW_TYPE_INT16, &jacobi_iterations_, "min=5 max=50 step=5 group=Simulation");
		TwAddVarRW(UI::Main()->tw_bar_, "simulation paused", TW_TYPE_BOOLCPP, &simulation_paused_, "group=Simulation");

		TwAddVarRW(UI::Main()->tw_bar_, "blur density", TW_TYPE_BOOLCPP, &blur_density_, "group=Blur");
		TwAddVarRW(UI::Main()->tw_bar_, "density blur sigma", TW_TYPE_FLOAT, &density_blur_sigma_, "min=0.1 max=2.0 step=0.1 group=Blur");
		TwAddVarRW(UI::Main()->tw_bar_, "density sample jittering", TW_TYPE_FLOAT, &density_sample_jittering_, "min=0.0 max=1.0 step=0.1 group=Blur");
		TwAddVarRW(UI::Main()->tw_bar_, "blur lighting", TW_TYPE_BOOLCPP, &blur_shadow_, "group=Blur");
		TwAddVarRW(UI::Main()->tw_bar_, "lighting blur sigma", TW_TYPE_FLOAT, &shadow_blur_sigma_, "min=0.1 max=2.0 step=0.1 group=Blur");
		TwAddVarRW(UI::Main()->tw_bar_, "lighting sample jittering", TW_TYPE_FLOAT, &shadow_sample_jittering_, "min=0.0 max=1.0 step=0.1 group=Blur");

		TwAddVarRW(UI::Main()->tw_bar_, "color absorption log10", TW_TYPE_FLOAT, &color_absorption_, "min=0.0 max=3 step=0.25 group=Lighting");
		TwAddVarRW(UI::Main()->tw_bar_, "light absorption log10", TW_TYPE_FLOAT, &light_absorption_, "min=0.0 max=3 step=0.25 group=Lighting");
		TwAddVarRW(UI::Main()->tw_bar_, "light intensity log10", TW_TYPE_FLOAT, &light_intensity_, "min=0.0 max=3 step=0.25 group=Lighting");
		TwAddVarRW(UI::Main()->tw_bar_, "ambient light log10", TW_TYPE_FLOAT, &ambient_light_, "min=-2 max=1 step=0.25 group=Lighting");
		TwAddVarRW(UI::Main()->tw_bar_, "smoke color", TW_TYPE_COLOR3F, &smoke_color_, "group=Lighting");
		TwAddVarRW(UI::Main()->tw_bar_, "light color", TW_TYPE_COLOR3F, &light_color_, "group=Lighting");
		TwAddVarRW(UI::Main()->tw_bar_, "enable shadows", TW_TYPE_BOOLCPP, &enable_shadows_, "group=Lighting");
		TwAddVarRW(UI::Main()->tw_bar_, "enable temperature color", TW_TYPE_BOOLCPP, &enable_temperature_color_, "group=Lighting");
	}

	void SmokeDemo::Update(float delta_time) {
		if (camera_rotation_) Camera::Main()->Orbit(delta_time * glm::radians(15.0f), 0);
		if (simulation_paused_) return;

		FillObstacle(delta_time);

		Advert(delta_time);
		AddImpulse(delta_time);
		ApplyBuoyancy(delta_time);
		ApplyVorticityConfinement(delta_time);

		ComputeDivergence();
		Jacobi();
		Projection();
		
		
	}

	void SmokeDemo::Render() {

		if (enable_shadows_) {
			render_shadow_->Activate();

			render_shadow_->SetUniform("u_jittering", shadow_sample_jittering_);
			render_shadow_->SetUniform("u_absorption", glm::pow(10, light_absorption_));

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_3D, i_density.ping);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_3D, i_obstacle);

			glBindImageTexture(0, i_shadow, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);
		}

		Blur();

		render_->Activate();

		render_->SetUniform("u_jittering", density_sample_jittering_);
		render_->SetUniform("u_absorption", glm::pow(10, color_absorption_));
		render_->SetUniform("u_light_intensity", glm::pow(10, light_intensity_));
		render_->SetUniform("u_smoke_color", smoke_color_);
		render_->SetUniform("u_light_color", light_color_);
		render_->SetUniform("u_ambient", glm::pow(10, ambient_light_));

		render_->SetUniform("u_enable_shadowing", (int)enable_shadows_);
		render_->SetUniform("u_temperature_color", (int)enable_temperature_color_);

		GLint viewport_size[4];
		glGetIntegerv(GL_VIEWPORT, viewport_size);

		glm::vec2 viewport_size_f = { viewport_size[2], viewport_size[3] };

		render_->SetUniform("u_viewport_size", viewport_size_f);
		render_->SetUniform("u_inverse_mvp", glm::inverse(Camera::Main()->view_mat()));

		GLuint t_density = blur_density_ ? i_density_blured.ping : i_density.ping;
		GLuint t_shadow = blur_shadow_ ? i_shadow_blured.ping : i_shadow;
		GLuint t_obstacle = obsticle_radius_ > 0 ? i_obstacle_blured.ping : i_obstacle;

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, t_density);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, t_shadow);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, t_obstacle);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_3D, i_temperature_blured.ping);

		fullscreen_quad_->Draw();
	}

	std::vector<float> SmokeDemo::ComputeGaussianKernel(float sigma) {
		float offset[5] = { 2.0f, 1.0f, 0.0f, 1.0f, 2.0f };
		float sum = 0.0f;
		std::vector<float> weights;

		for (int i = 0; i < 5; i++) {
			weights.push_back(glm::exp(-offset[i] * offset[i] / (sigma* sigma * 2.0f)));
			sum += weights[i];
		}

		for (int i = 0; i < 5; i++) {
			weights[i] /= sum;
		}

		return weights;
	}

	void SmokeDemo::GenTexture(GLuint& tex, GLenum internal_format) {
		glActiveTexture(GL_TEXTURE0);
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_3D, tex);
		glTexStorage3D(GL_TEXTURE_3D, 1, internal_format, GLOBAL_SIZE_X, GLOBAL_SIZE_Y, GLOBAL_SIZE_Z);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
		glBindTexture(GL_TEXTURE_3D, 0);
	}

	void SmokeDemo::GenTexture(Slab& slab, GLenum internal_format) {
		GenTexture(slab.ping, internal_format);
		GenTexture(slab.pong, internal_format);
	}

	void SmokeDemo::FillObstacle(float delta_time) {
		
		glm::vec3 pos = glm::vec3(GLOBAL_SIZE_X * obsticle_position_.x, GLOBAL_SIZE_Y * obsticle_position_.y, GLOBAL_SIZE_Z * obsticle_position_.z);
		glm::vec3 v = glm::vec3(0);

		if (enable_obstacle_motion_) {
			if (!enable_obstacle_motion_prev_) {
				obstacle_current_pos_ = glm::vec3(0, GLOBAL_SIZE_Y * obsticle_position_.y, GLOBAL_SIZE_Z * obsticle_position_.z);;
				obstacle_current_v_ = glm::vec3(100, 0, 0);
				pos = obstacle_current_pos_;
				v = obstacle_current_v_;

				enable_obstacle_motion_prev_ = true;
			}
			else {
				obstacle_current_pos_ += obstacle_current_v_ * delta_time;
				pos = obstacle_current_pos_;
				v = obstacle_current_v_;
			}
		}
		else {
			enable_obstacle_motion_prev_ = false;
		}

		init_fill_obstacle_->Activate();
		init_fill_obstacle_->SetUniform("u_location", pos);
		init_fill_obstacle_->SetUniform("u_radius", obsticle_radius_);
		init_fill_obstacle_->SetUniform("u_velocity", v);

		glBindImageTexture(0, i_obstacle, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glBindImageTexture(1, i_obstacle_v, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
	}

	void SmokeDemo::Advert(float delta_time) {
		//advert velocity
		update_advect_rgba_->Activate();
		update_advect_rgba_->SetUniform("u_time_step", delta_time);
		update_advect_rgba_->SetUniform("u_dissipation", 1.0f - glm::pow(10, velocity_dissipation_));

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(2, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(3, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();

		//advert temperature
		update_advect_r_->Activate();
		update_advect_r_->SetUniform("u_time_step", delta_time);
		update_advect_r_->SetUniform("u_dissipation", 1.0f);

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);

		glBindImageTexture(2, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_phi_n_1_hat, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		update_advect_r_->SetUniform("u_time_step", -delta_time);

		glBindImageTexture(2, i_phi_n_1_hat, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_phi_n_hat, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		update_advect_r_mac_cormack_->Activate();
		update_advect_r_mac_cormack_->SetUniform("u_time_step", delta_time);
		update_advect_r_mac_cormack_->SetUniform("u_dissipation", 1.0f - glm::pow(10, temperature_dissipation_));
		update_advect_r_mac_cormack_->SetUniform("u_decay", glm::pow(10, temperature_decay_));

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(2, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_temperature.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glBindImageTexture(4, i_phi_n_1_hat, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(5, i_phi_n_hat, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_temperature.Swap();

		//advert density
		update_advect_r_->Activate();
		update_advect_r_->SetUniform("u_time_step", delta_time);
		update_advect_r_->SetUniform("u_dissipation", 1.0f);

		glBindImageTexture(2, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_phi_n_1_hat, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		update_advect_r_->SetUniform("u_time_step", -delta_time);
		update_advect_r_->SetUniform("u_dissipation", 1.0f);

		glBindImageTexture(2, i_phi_n_1_hat, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_phi_n_hat, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		update_advect_r_mac_cormack_->Activate();
		update_advect_r_mac_cormack_->SetUniform("u_time_step", delta_time);
		update_advect_r_mac_cormack_->SetUniform("u_dissipation", 1.0f - glm::pow(10, density_dissipation_));
		update_advect_r_mac_cormack_->SetUniform("u_decay", glm::pow(10, density_decay_));

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(2, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_density.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glBindImageTexture(4, i_phi_n_1_hat, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(5, i_phi_n_hat, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_density.Swap();

		
	}

	void SmokeDemo::AddImpulse(float delta_time) {
		if (enable_injection_) {
			update_splat_->Activate();
			update_splat_->SetUniform("u_time_step", delta_time);
			update_splat_->SetUniform("u_location", glm::vec3(GLOBAL_SIZE_X * injection_location_.x, GLOBAL_SIZE_Y * injection_location_.y, GLOBAL_SIZE_Z * injection_location_.z));
			update_splat_->SetUniform("u_radius", temperature_injection_radius_);
			update_splat_->SetUniform("u_intensity", glm::pow(10, temperature_injection_intensity_));

			glBindImageTexture(0, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_temperature.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_temperature.Swap();

			update_splat_->SetUniform("u_radius", density_injection_radius_);
			update_splat_->SetUniform("u_intensity", glm::pow(10, density_injection_intensity_));

			glBindImageTexture(0, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_density.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_density.Swap();
		}
		

		static float timer = 3.0f;
		if (timer < 0) {
			if (add_explosion_) {
				timer = explosion_timer_;
				AddExplosion(delta_time);
			}
		}
		else {
			timer -= delta_time;
		}
	}

	void SmokeDemo::AddExplosion(float delta_time) {
		float intensity = glm::linearRand(glm::pow(10, explosion_force_min_), glm::pow(10, explosion_force_max_));

		update_explosion_->Activate();

		update_explosion_->SetUniform("u_time_step", 1.0);
		update_explosion_->SetUniform("u_location", glm::vec3(GLOBAL_SIZE_X * injection_location_.x, GLOBAL_SIZE_Y * injection_location_.y, GLOBAL_SIZE_Z * injection_location_.z));
		update_explosion_->SetUniform("u_radius", explosion_concetration_);
		update_explosion_->SetUniform("u_intensity", intensity);

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();

		update_splat_->Activate();
		update_splat_->SetUniform("u_time_step", 1.0);
		update_splat_->SetUniform("u_location", glm::vec3(GLOBAL_SIZE_X * injection_location_.x, GLOBAL_SIZE_Y * injection_location_.y, GLOBAL_SIZE_Z * injection_location_.z));
		update_splat_->SetUniform("u_radius", explosion_concetration_);
		update_splat_->SetUniform("u_intensity", glm::pow(10, explosion_temperature_ratio_) * intensity / 10.0f);

		glBindImageTexture(0, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(1, i_temperature.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_temperature.Swap();

		update_splat_->SetUniform("u_radius", density_injection_radius_);
		update_splat_->SetUniform("u_intensity", glm::pow(10, explosion_injection_ratio_) * intensity / 10.0f);

		glBindImageTexture(0, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(1, i_density.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_density.Swap();
	}

	void SmokeDemo::ApplyBuoyancy(float delta_time) {
		if (!enable_buoyancy_) return;

		update_buoyancy_->Activate();
		update_buoyancy_->SetUniform("u_ambient_temperature", ambient_temperature_);
		update_buoyancy_->SetUniform("u_time_step", delta_time);
		update_buoyancy_->SetUniform("u_buoyancy", glm::pow(10, buoyancy_));
		update_buoyancy_->SetUniform("u_weight", glm::pow(10, weight_));

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(2, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();
	}

	void SmokeDemo::ApplyVorticityConfinement(float delta_time) {
		update_vorticity_->Activate();

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_vorticity, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		update_confinement_->Activate();
		update_confinement_->SetUniform("u_time_step", delta_time);
		update_confinement_->SetUniform("u_vorticity_strength", vorticity_strength_);

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_vorticity, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();
	}

	void SmokeDemo::ComputeDivergence() {
		update_divergence_->Activate();

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(2, i_obstacle_v, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(3, i_divergence, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
	}

	void SmokeDemo::Jacobi() {
		init_fill_r_->Activate();

		glBindImageTexture(0, i_pressure.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		update_jacobi_->Activate();

		glBindImageTexture(0, i_divergence, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);

		for (int i = 0; i < jacobi_iterations_; i++) {
			glBindImageTexture(2, i_pressure.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(3, i_pressure.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_pressure.Swap();
		}
	}

	void SmokeDemo::Projection() {
		update_gradient_->Activate();

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(2, i_pressure.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_obstacle_v, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(4, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();
	}

	void SmokeDemo::Blur() {
		if (enable_shadows_ && blur_shadow_) {
			render_blur_->Activate();

			render_blur_->SetUniform("u_weights[0]", ComputeGaussianKernel(shadow_blur_sigma_));
			render_blur_->SetUniform("u_pass_num", 0);

			glBindImageTexture(0, i_shadow, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_shadow_blured.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			render_blur_->SetUniform("u_pass_num", 1);

			glBindImageTexture(0, i_shadow_blured.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_shadow_blured.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_shadow_blured.Swap();

			render_blur_->SetUniform("u_pass_num", 2);

			glBindImageTexture(0, i_shadow_blured.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_shadow_blured.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_shadow_blured.Swap();
		}

		if (blur_density_) {
			render_blur_->Activate();

			render_blur_->SetUniform("u_weights[0]", ComputeGaussianKernel(density_blur_sigma_));

			render_blur_->SetUniform("u_pass_num", 0);

			glBindImageTexture(0, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_density_blured.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			render_blur_->SetUniform("u_pass_num", 1);

			glBindImageTexture(0, i_density_blured.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_density_blured.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_density_blured.Swap();

			render_blur_->SetUniform("u_pass_num", 2);

			glBindImageTexture(0, i_density_blured.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_density_blured.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_density_blured.Swap();
		}

		if (enable_temperature_color_) {
			render_blur_->Activate();

			render_blur_->SetUniform("u_weights[0]", ComputeGaussianKernel(density_blur_sigma_));

			render_blur_->SetUniform("u_pass_num", 0);

			glBindImageTexture(0, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_temperature_blured.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			render_blur_->SetUniform("u_pass_num", 1);

			glBindImageTexture(0, i_temperature_blured.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_temperature_blured.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_temperature_blured.Swap();

			render_blur_->SetUniform("u_pass_num", 2);

			glBindImageTexture(0, i_temperature_blured.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_temperature_blured.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_temperature_blured.Swap();
		}

		if (obsticle_radius_ > 0) {
			render_blur_->Activate();

			render_blur_->SetUniform("u_weights[0]", ComputeGaussianKernel(density_blur_sigma_));

			render_blur_->SetUniform("u_pass_num", 0);

			glBindImageTexture(0, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_obstacle_blured.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			render_blur_->SetUniform("u_pass_num", 1);

			glBindImageTexture(0, i_obstacle_blured.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_obstacle_blured.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_obstacle_blured.Swap();

			render_blur_->SetUniform("u_pass_num", 2);

			glBindImageTexture(0, i_obstacle_blured.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(1, i_obstacle_blured.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_obstacle_blured.Swap();
		}
	}

	

	

	

}
