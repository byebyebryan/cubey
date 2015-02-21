#include "SmokeDemo.h"

#include "glm/gtc/random.hpp"
#include "glm/gtx/color_space.hpp"

namespace cubey {
	void SmokeDemo::FillObstacle() {

		if (!obstacle_enabled()) {
			sp_fill_boundary_->Activate();

			glBindImageTexture(0, t3d_obstacle_.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, t3d_obstacle_.format);

			glDispatchCompute(t3d_obstacle_.size.x / 8, t3d_obstacle_.size.y / 8, t3d_obstacle_.size.z / 8);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);
		}
		else {
			if (obstacle_type() == 0) {
				sp_fill_obstacle_ball_->Activate();
				sp_fill_obstacle_ball_->SetUniform("u_location", obstacle_position());
				sp_fill_obstacle_ball_->SetUniform("u_radius", obstacle_radius());

				glBindImageTexture(0, t3d_obstacle_.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, t3d_obstacle_.format);

				glDispatchCompute(t3d_obstacle_.size.x / 8, t3d_obstacle_.size.y / 8, t3d_obstacle_.size.z / 8);
				glMemoryBarrier(GL_ALL_BARRIER_BITS);
			}
			else if (obstacle_type() == 1) {
				sp_fill_obstacle_box_->Activate();
				sp_fill_obstacle_box_->SetUniform("u_location", obstacle_position());
				sp_fill_obstacle_box_->SetUniform("u_extend", glm::vec3(obstacle_radius()));

				glBindImageTexture(0, t3d_obstacle_.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, t3d_obstacle_.format);

				glDispatchCompute(t3d_obstacle_.size.x / 8, t3d_obstacle_.size.y / 8, t3d_obstacle_.size.z / 8);
				glMemoryBarrier(GL_ALL_BARRIER_BITS);
			}
			
		}
	}

	void SmokeDemo::AdvertSL(const Tex3D& source, const Tex3D& target, float dissipation, float delta_time) {
		ShaderProgram* p = (target.format == GL_RGBA16F ? sp_advect_rgba_ : sp_advect_r_);

		p->Activate();

		p->SetUniform("u_time_step", delta_time);
		p->SetUniform("u_dissipation", dissipation);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, t3d_obstacle_.gl);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, source.gl);

		glBindImageTexture(0, target.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, target.format);

		glDispatchCompute(target.size.x / 8, target.size.y / 8, target.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
	}
	void SmokeDemo::AdvertMC(const Tex3D& source, const Tex3D& target, float dissipation, float decay, float delta_time) {
		Tex3D phi_n_1_hat = (target.format == GL_RGBA16F ? t3d_phi_n_1_hat_rgba_ : t3d_phi_n_1_hat_r_);
		Tex3D phi_n_hat = (target.format == GL_RGBA16F ? t3d_phi_n_hat_rgba_: t3d_phi_n_hat_r_);

		AdvertSL(source, phi_n_1_hat, 0, delta_time);
		AdvertSL(phi_n_1_hat, phi_n_hat, 0, -delta_time);

		ShaderProgram* p = (target.format == GL_RGBA16F ? sp_advect_r_mac_cormack_rgba_ : sp_advect_r_mac_cormack_r_);
		p->Activate();

		p->SetUniform("u_time_step", delta_time);
		p->SetUniform("u_dissipation", dissipation);
		p->SetUniform("u_decay", decay);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, t3d_obstacle_.gl);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, phi_n_1_hat.gl);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_3D, phi_n_hat.gl);

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_3D, source.gl);

		glBindImageTexture(0, target.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, target.format);

		glDispatchCompute(target.size.x / 8, target.size.y / 8, target.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
	}

	void SmokeDemo::Advert(float delta_time) {
		//advert velocity
		AdvertSL(s3d_velocity_.ping, s3d_velocity_.pong, Pow10(velocity_dissipation_log10()), delta_time);
		s3d_velocity_.Swap();

		//advert temperature
		//AdvertSL(s3d_temperature_.ping, s3d_temperature_.pong, Pow10(temperature_dissipation_log10()), delta_time);
		AdvertMC(s3d_temperature_.ping, s3d_temperature_.pong, Pow10(temperature_dissipation_log10()), Pow10(temperature_decay_log10()), delta_time);
		s3d_temperature_.Swap();

		//advert density
		//AdvertSL(s3d_density_.ping, s3d_density_.pong, Pow10(density_dissipation_log10()), delta_time);
		AdvertMC(s3d_density_.ping, s3d_density_.pong, Pow10(density_dissipation_log10()), Pow10(density_decay_log10()), delta_time);
		s3d_density_.Swap();
	}

	void SmokeDemo::InjectForce(const glm::vec3& position, float sigma, float intensity, float delta_time) {
		ShaderProgram* p = sp_inject_splat_v_;

		p->Activate();

		p->SetUniform("u_time_step", delta_time);
		p->SetUniform("u_location", position);
		p->SetUniform("u_sigma", sigma);
		p->SetUniform("u_intensity", intensity);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glBindImageTexture(0, s3d_velocity_.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, s3d_velocity_.pong.format);

		glDispatchCompute(s3d_velocity_.pong.size.x / 8, s3d_velocity_.pong.size.y / 8, s3d_velocity_.pong.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		s3d_velocity_.Swap();
	}
	void SmokeDemo::InjectForceLinear(const glm::vec3& position, const glm::vec3& direction, float sigma, float intensity, float delta_time) {
		ShaderProgram* p = sp_inject_splat_v_linear_;

		p->Activate();

		p->SetUniform("u_time_step", delta_time);
		p->SetUniform("u_location", position);
		p->SetUniform("u_sigma", sigma);
		p->SetUniform("u_intensity", intensity);
		p->SetUniform("u_direction", direction);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glBindImageTexture(0, s3d_velocity_.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, s3d_velocity_.pong.format);

		glDispatchCompute(s3d_velocity_.pong.size.x / 8, s3d_velocity_.pong.size.y / 8, s3d_velocity_.pong.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		s3d_velocity_.Swap();
	}
	void SmokeDemo::InjectTemperature(const glm::vec3& position, float sigma, float intensity, float delta_time) {
		ShaderProgram* p = sp_inject_splat_r_;

		p->Activate();

		p->SetUniform("u_time_step", delta_time);
		p->SetUniform("u_location", position);
		p->SetUniform("u_sigma", sigma);
		p->SetUniform("u_intensity", intensity);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_temperature_.ping.gl);

		glBindImageTexture(0, s3d_temperature_.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, s3d_temperature_.pong.format);

		glDispatchCompute(s3d_temperature_.pong.size.x / 8, s3d_temperature_.pong.size.y / 8, s3d_temperature_.pong.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		s3d_temperature_.Swap();
	}

	void SmokeDemo::InjectDensity(const glm::vec3& position, float sigma, const glm::vec4& intensity, float delta_time) {
		ShaderProgram* p = sp_inject_splat_rgba_;

		p->Activate();

		p->SetUniform("u_time_step", delta_time);
		p->SetUniform("u_location", position);
		p->SetUniform("u_sigma", sigma);
		p->SetUniform("u_intensity", intensity);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_density_.ping.gl);

		glBindImageTexture(0, s3d_density_.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, s3d_density_.pong.format);

		glDispatchCompute(s3d_density_.pong.size.x / 8, s3d_density_.pong.size.y / 8, s3d_density_.pong.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		s3d_density_.Swap();
	}

	void SmokeDemo::Inject(float delta_time) {
		if (!injection_enabled()) return;

		static float y_rotation = 0.0f;

		float hue = injection_density_color_hue();
		if (injection_density_color_random_hue()) {
			hue = glm::linearRand(0.0f, 360.0f);
		}

		float d_hue = 360.0f / injection_count();

		float d_y_rot = glm::radians(360.0f / injection_count());
		float y_rot = glm::radians(y_rotation);

		for (int i = 0; i < injection_count(); i++) {
			glm::vec3 dir = glm::rotateX(glm::vec3(0, 1, 0), glm::radians(injection_x_rotation()));
			dir = glm::rotateY(dir, y_rot);
			glm::vec3 pos = injection_reference_position() + glm::rotateY(glm::vec3(0, 0, 1), y_rot) * injection_reference_distance();

			float density = Pow10(injection_density_intensity_log10()) / 3.0f;

			glm::vec3 injection_density = glm::rgbColor(glm::vec3(hue, injection_density_color_saturation(), injection_density_color_value())) * density;

			InjectDensity(pos, injection_density_sigma(), glm::vec4(injection_density, 0.0f), delta_time);
			InjectTemperature(pos, injection_temperature_sigma(), Pow10(injection_temperature_intensity_log10()), delta_time);
			if (injection_linear_foce()) {
				InjectForceLinear(pos, dir, injection_force_sigma(), Pow10(injection_force_intensity_log10()), delta_time);
			}
			else {
				InjectForce(pos, injection_force_sigma(), Pow10(injection_force_intensity_log10()), delta_time);
			}

			y_rot += d_y_rot;
			hue += d_hue;
		}

		if (injection_y_rotation_enabled()) {
			y_rotation += injection_y_rotation_speed() * delta_time;

			if (y_rotation > 180.0f) y_rotation -= 360.0f;
			if (y_rotation < -180.0f) y_rotation += 360.0f;
		}
	}

	void SmokeDemo::Explosion(float delta_time) {
		if (!explosion_enabled()) return;

		static float timer = 1.0f;

		if (timer <= 0.0f) {
			timer = explosion_timer();

			InjectForce(explosion_position(), explosion_force_sigma(), Pow10(explosion_force_intensity_log10()), 1.0f);

			float density_intensity = Pow10(explosion_density_intensity_log10());
			glm::vec3 density_injection = density_intensity * glm::rgbColor(glm::vec3(explosion_density_color_hue(), explosion_density_color_saturation(), explosion_density_color_value())) / 3.0f;

			if (explosion_density_color_random_hue()) {
				density_injection = density_intensity * glm::rgbColor(glm::vec3(glm::linearRand(0.0f, 360.0f), explosion_density_color_saturation(), explosion_density_color_value())) / 3.0f;
			}

			InjectDensity(explosion_position(), explosion_density_sigma(), glm::vec4(density_injection, 0.0f), 1.0f);
			InjectTemperature(explosion_position(), explosion_temperature_sigma(), Pow10(explosion_temperature_intensity_log10()), 1.0f);
		}
		else {
			timer -= delta_time;
		}
	}
	
	void SmokeDemo::ApplyBuoyancy(float delta_time) {
		if (!buoyancy_enabled()) return;

		sp_buoyancy_->Activate();

		sp_buoyancy_->SetUniform("u_ambient_temperature", ambient_temperature());
		sp_buoyancy_->SetUniform("u_time_step", delta_time);
		sp_buoyancy_->SetUniform("u_buoyancy", Pow10(buoyancy_log10()));
		sp_buoyancy_->SetUniform("u_weight", Pow10(weight_log10()));

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, s3d_temperature_.ping.gl);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, s3d_density_.ping.gl);

		glBindImageTexture(0, s3d_velocity_.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, s3d_velocity_.pong.format);

		glDispatchCompute(s3d_velocity_.pong.size.x / 8, s3d_velocity_.pong.size.y / 8, s3d_velocity_.pong.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		s3d_velocity_.Swap();
	}

	void SmokeDemo::VorticityConfinement(float delta_time) {
		if (!vorticity_confinement_enabled()) return;

		sp_vorticity_->Activate();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glBindImageTexture(0, t3d_vorticity_.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, t3d_vorticity_.format);
		glDispatchCompute(t3d_vorticity_.size.x / 8, t3d_vorticity_.size.y / 8, t3d_vorticity_.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		sp_confinement_->Activate();

		sp_confinement_->SetUniform("u_time_step", delta_time);
		sp_confinement_->SetUniform("u_vorticity_strength", vorticity_confinement_strength());

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, t3d_vorticity_.gl);

		glBindImageTexture(0, s3d_velocity_.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, s3d_velocity_.pong.format);
		glDispatchCompute(s3d_velocity_.pong.size.x / 8, s3d_velocity_.pong.size.y / 8, s3d_velocity_.pong.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		s3d_velocity_.Swap();
	}

	void SmokeDemo::ComputeDivergence() {
		sp_divergence_->Activate();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, t3d_obstacle_.gl);

		glBindImageTexture(0, t3d_divergence_.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, t3d_divergence_.format);

		glDispatchCompute(t3d_divergence_.size.x / 8, t3d_divergence_.size.y / 8, t3d_divergence_.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
	}

	void SmokeDemo::Jacobi() {

		ClearTexture(s3d_pressure_.ping);

		sp_jacobi_->Activate();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, t3d_divergence_.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, t3d_obstacle_.gl);

		for (int i = 0; i < solver_iterations(); i++) {
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_3D, s3d_pressure_.ping.gl);

			glBindImageTexture(0, s3d_pressure_.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, s3d_pressure_.pong.format);

			glDispatchCompute(s3d_pressure_.pong.size.x / 8, s3d_pressure_.pong.size.y / 8, s3d_pressure_.pong.size.z / 8);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);
			s3d_pressure_.Swap();
		}
	}

	void SmokeDemo::Projection() {
		sp_project_->Activate();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_velocity_.ping.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, t3d_obstacle_.gl);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, s3d_pressure_.ping.gl);

		glBindImageTexture(0, s3d_velocity_.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, s3d_velocity_.pong.format);
		glDispatchCompute(s3d_velocity_.pong.size.x / 8, s3d_velocity_.pong.size.y / 8, s3d_velocity_.pong.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		s3d_velocity_.Swap();
	}
}