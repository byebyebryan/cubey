#include "SmokeDemo.h"

#include "glm/gtc/random.hpp"

namespace cubey {

	void SmokeDemo::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_TEXTURE_3D);
		//glClearColor(0.5, 0.5, 0.5, 1.0);

		ShaderManager::BufferCleaner shader_cleaner;

		sp_fill_rgba_ = ShaderManager::Get()->CreateProgram("smoke_compute_util.__CS_FILL_RGBA__");
		sp_fill_r_ = ShaderManager::Get()->CreateProgram("smoke_compute_util.__CS_FILL_R__");
		sp_fill_boundary_ = ShaderManager::Get()->CreateProgram("smoke_compute_util.__CS_FILL_BOUNDARY__");
		sp_fill_obstacle_ball_ = ShaderManager::Get()->CreateProgram("smoke_compute_util.__CS_FILL_OBSTACLE_BALL__");
		sp_fill_obstacle_box_ = ShaderManager::Get()->CreateProgram("smoke_compute_util.__CS_FILL_OBSTACLE_BOX__");

		sp_advect_rgba_ = ShaderManager::Get()->CreateProgram("smoke_compute_sim.__CS_ADVECT_RGBA__");
		sp_advect_r_ = ShaderManager::Get()->CreateProgram("smoke_compute_sim.__CS_ADVECT_R__");
		sp_advect_r_mac_cormack_rgba_ = ShaderManager::Get()->CreateProgram("smoke_compute_sim.__CS_ADVECT_MAC_CORMACK_RGBA__");
		sp_advect_r_mac_cormack_r_ = ShaderManager::Get()->CreateProgram("smoke_compute_sim.__CS_ADVECT_MAC_CORMACK_R__");
		sp_divergence_ = ShaderManager::Get()->CreateProgram("smoke_compute_sim.__CS_DIVERGENCE__");
		sp_jacobi_ = ShaderManager::Get()->CreateProgram("smoke_compute_sim.__CS_JACOBI__");
		sp_project_ = ShaderManager::Get()->CreateProgram("smoke_compute_sim.__CS_PROJECTION__");

		sp_inject_splat_v_ = ShaderManager::Get()->CreateProgram("smoke_compute_ext.__CS_INJECTION_SPLAT_V__");
		sp_inject_splat_v_linear_ = ShaderManager::Get()->CreateProgram("smoke_compute_ext.__CS_INJECTION_SPLAT_V_LINEAR__");
		sp_inject_splat_rgba_ = ShaderManager::Get()->CreateProgram("smoke_compute_ext.__CS_INJECTION_SPLAT_RGBA__");
		sp_inject_splat_r_ = ShaderManager::Get()->CreateProgram("smoke_compute_ext.__CS_INJECTION_SPLAT_R__");
		sp_buoyancy_ = ShaderManager::Get()->CreateProgram("smoke_compute_ext.__CS_BUOYANCY__");
		sp_vorticity_ = ShaderManager::Get()->CreateProgram("smoke_compute_ext.__CS_VORTICITY__");
		sp_confinement_ = ShaderManager::Get()->CreateProgram("smoke_compute_ext.__CS_CONFINEMENT__");

		sp_blur_rgba_ = ShaderManager::Get()->CreateProgram("smoke_render_util.__CS_GAUSSIAN_BLUR_RGBA__");
		sp_blur_r_ = ShaderManager::Get()->CreateProgram("smoke_render_util.__CS_GAUSSIAN_BLUR_R__");
		sp_lighting_ = ShaderManager::Get()->CreateProgram("smoke_render_util.__CS_SHADOW__");

		sp_render_ = ShaderManager::Get()->CreateProgram("smoke_render.VS.FS");

		fullscreen_quad_ = PrimitiveFactory::FullScreenQuad()->CreateInstance(sp_render_);

		TwUI::Get()->AddDefaultInfo();

		TwBarInit();

		//TwDefine("main/Camera opened=false");
		TwDefine("main/Simulation opened=false");
		TwDefine("main/Obstacle opened=false");
		TwDefine("main/Advection opened=false");
		TwDefine("main/Injection opened=false");
		TwDefine("main/Explosion opened=false");
		TwDefine("main/Buoyancy opened=false");
		TwDefine("main/Vorticity opened=false");
		TwDefine("main/Projection opened=false");
		TwDefine("main/Smoke opened=false");
		TwDefine("main/Lighting opened=false");
		TwDefine("main/Shadows opened=false");
		TwDefine("main/Radiance opened=false");

		TwAddButton(TwUI::Get()->main_bar_, "change resolution", &SmokeDemo::ChangeResolution, this, "group=Simulation");
		TwAddButton(TwUI::Get()->main_bar_, "save config", &SmokeDemo::SaveConfig, this, "group=XML");

		EventChannel<Engine::DropEvent>::DirtyAdd([this](const Engine::DropEvent& e){
			ReadFromFile(e.names[0]);
			std::cout << "Loaded config file : " << e.names[0] << std::endl;
			StartUp();
		});

		MainCamera::Get()->transform_.TranslateTo(glm::vec3(0, 0, -1.5f));
		MainCamera::Get()->Orbit(glm::radians(30.0f), glm::radians(-15.0f));
		MainCamera::Get()->mouse_wheel_speed_ = 1.0f;
		MainCamera::Get()->movement_speed_ = 1.0f;
	}

	void SmokeDemo::StartUp() {
		PrepTextures();
	}

	void SmokeDemo::Update(float delta_time) {

		if (camera_rotation_enabled()) {
			MainCamera::Get()->Orbit(delta_time * glm::radians(camera_rotation_speed()), 0);
		}
		if (simulation_paused()) return;

		FillObstacle();

		Advert(delta_time);
		
		Inject(delta_time);
		Explosion(delta_time);
		ApplyBuoyancy(delta_time);

		VorticityConfinement(delta_time);

		ComputeDivergence();
		Jacobi();
		Projection();
	}

	void SmokeDemo::Render() {

		Tex3D obstacle = t3d_obstacle_;
		Tex3D temperature = s3d_temperature_.ping;
		Tex3D density = s3d_density_.ping;
		Tex3D light = t3d_lighting_;

		if (obstacle_enabled()) {
			GaussianBlur(t3d_obstacle_, t3d_obstacle_blured_, 1.0f);
			obstacle = t3d_obstacle_blured_;
		}
		if (radiance_color_enabled()) {
			GaussianBlur(s3d_temperature_.ping, t3d_temperature_blured_, smoke_blur_sigma());
			temperature = t3d_temperature_blured_;
		}

		ComputeLighting(obstacle);

		if (shadows_blur_enabled()) {
			GaussianBlur(t3d_lighting_, t3d_lighting_blured_, shadows_blur_sigma());
			light = t3d_lighting_blured_;
		}

		if (smoke_blur_enabled()) {
			GaussianBlur(s3d_density_.ping, t3d_density_blured_, smoke_blur_sigma());
			density = t3d_density_blured_;
		}

		sp_render_->Activate();

		GLint viewport_size[4];
		glGetIntegerv(GL_VIEWPORT, viewport_size);

		glm::vec2 viewport_size_f = { viewport_size[2], viewport_size[3] };

		sp_render_->SetUniform("u_viewport_size", viewport_size_f);
		sp_render_->SetUniform("u_inverse_mvp", glm::inverse(MainCamera::Get()->view_mat()));

		sp_render_->SetUniform("u_step_size", 1.0f/smoke_sampling_resolution());
		sp_render_->SetUniform("u_jittering", smoke_sampling_jittering());

		sp_render_->SetUniform("u_light_color", light_color().vec);
		sp_render_->SetUniform("u_light_intensity", Pow10(light_intensity_log10()));
		sp_render_->SetUniform("u_absorption", Pow10(smoke_color_absorption_log10()));
		sp_render_->SetUniform("u_ambient", Pow10(ambient_light_log10()));

		sp_render_->SetUniform("u_enable_shadowing", (int)shadows_enabled());
		sp_render_->SetUniform("u_temperature_color", (int)radiance_color_enabled());
		sp_render_->SetUniform("u_temperature_color_falloff", Pow10(radiance_color_falloff_log10()));

		sp_render_->SetUniform("u_density_factor", Pow10(smoke_density_factor_log10()));

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, density.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, light.gl);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, obstacle.gl);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_3D, temperature.gl);

		fullscreen_quad_->Draw();
	}

	void SmokeDemo::ComputeLighting(const Tex3D& obstacle) {
		if (!shadows_enabled()) return;

		sp_lighting_->Activate();

		sp_lighting_->SetUniform("u_step_size", 1.0f / shadows_sampling_resolution());
		sp_lighting_->SetUniform("u_light_position", light_position());
		sp_lighting_->SetUniform("u_absorption", Pow10(light_absorption_log10()));
		sp_lighting_->SetUniform("u_jittering", shadows_sampling_jittering());

		sp_lighting_->SetUniform("u_density_factor", Pow10(smoke_density_factor_log10()));

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, s3d_density_.ping.gl);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, obstacle.gl);

		glBindImageTexture(0, t3d_lighting_.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, t3d_lighting_.format);

		glDispatchCompute(t3d_lighting_.size.x / 8, t3d_lighting_.size.y / 8, t3d_lighting_.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
	}

	void TW_CALL SmokeDemo::SaveConfig(void* client_data) {
		SmokeDemo* p = (SmokeDemo*)client_data;
		unsigned int id = glm::linearRand(100000, 999999);
		std::string file_name = "config" + std::to_string(id) + ".xml";
		p->WriteToFile(file_name);
		std::cout << "Saved config file : " << file_name << std::endl;
	}

	void TW_CALL SmokeDemo::ChangeResolution(void* client_data) {
		SmokeDemo* p = (SmokeDemo*)client_data;
		p->StartUp();
	}

	

	

	

}
