#pragma once
#include "cubey.h"

namespace cubey {
	
	struct Tex3D {
		GLuint gl;
		GLenum format;
		glm::ivec3 size;
	};

	struct Slab {
		Tex3D ping;
		Tex3D pong;

		void Swap() {
			Tex3D t = ping;
			ping = pong;
			pong = t;
		}
	};

	

	class SmokeDemo : public EngineEventsBase, public AutoXmlBase<SmokeDemo> {
	public:
		void Init() override;
		void StartUp() override;
		void Update(float delta_time) override;
		void Render() override;

		static void TW_CALL SaveConfig(void* client_data);
		static void TW_CALL ChangeResolution(void* client_data);

	private:

		void FillObstacle();

		void Advert(float delta_time);
		void AdvertSL(const Tex3D& source, const Tex3D& target, float dissipation, float delta_time);
		void AdvertMC(const Tex3D& source, const Tex3D& target, float dissipation, float decay, float delta_time);

		void Inject(float delta_time);
		void InjectForce(const glm::vec3& position, float sigma, float intensity, float delta_time);
		void InjectForceLinear(const glm::vec3& position, const glm::vec3& direction, float sigma, float intensity, float delta_time);
		void InjectTemperature(const glm::vec3& position, float sigma, float intensity, float delta_time);
		void InjectDensity(const glm::vec3& position, float sigma, const glm::vec4& intensity, float delta_time);

		void Explosion(float delta_time);

		void ApplyBuoyancy(float delta_time);

		void VorticityConfinement(float delta_time);

		void ComputeDivergence();
		void Jacobi();
		void Projection();

		void ComputeLighting(const Tex3D& obstacle);

		void PrepTextures();

		void GenTexture(Tex3D& tex, GLenum internal_formatm, const glm::ivec3& size);
		void GenTexture(Slab& slab, GLenum internal_format, const glm::ivec3& size);

		void DelTexture(Tex3D& tex);
		void DelTexture(Slab& slab);

		void ClearTexture(const Tex3D& tex);
		std::vector<float> ComputeGaussianKernel(int size, float sigma);
		void GaussianBlur(const Tex3D& source, const Tex3D& target, float sigma);
		float Pow10(float x) { return glm::pow(10.0f, x); }

		MeshInstance* fullscreen_quad_;

		Slab s3d_velocity_;
		Slab s3d_temperature_;
		Slab s3d_density_;
		Slab s3d_pressure_;
		Tex3D t3d_divergence_;
		Tex3D t3d_obstacle_;
		Tex3D t3d_vorticity_;

		Tex3D t3d_phi_n_1_hat_rgba_;
		Tex3D t3d_phi_n_hat_rgba_;
		Tex3D t3d_phi_n_1_hat_r_;
		Tex3D t3d_phi_n_hat_r_;

		Tex3D t3d_lighting_;

		Tex3D t3d_density_blured_;
		Tex3D t3d_lighting_blured_;
		Tex3D t3d_temperature_blured_;
		Tex3D t3d_obstacle_blured_;

		Slab s3d_blur_r_;
		Slab s3d_blur_rgba_;

		ShaderProgram* sp_fill_rgba_;
		ShaderProgram* sp_fill_r_;
		ShaderProgram* sp_fill_boundary_;
		ShaderProgram* sp_fill_obstacle_ball_;
		ShaderProgram* sp_fill_obstacle_box_;

		ShaderProgram* sp_advect_rgba_;
		ShaderProgram* sp_advect_r_;
		ShaderProgram* sp_advect_r_mac_cormack_rgba_;
		ShaderProgram* sp_advect_r_mac_cormack_r_;

		ShaderProgram* sp_inject_splat_v_;
		ShaderProgram* sp_inject_splat_v_linear_;
		ShaderProgram* sp_inject_splat_rgba_;
		ShaderProgram* sp_inject_splat_r_;

		ShaderProgram* sp_buoyancy_;

		ShaderProgram* sp_vorticity_;
		ShaderProgram* sp_confinement_;

		ShaderProgram* sp_divergence_;
		ShaderProgram* sp_jacobi_;
		ShaderProgram* sp_project_;

		ShaderProgram* sp_blur_rgba_;
		ShaderProgram* sp_blur_r_;
		ShaderProgram* sp_lighting_;
		ShaderProgram* sp_render_;

		AUTO_XML_VAR_TW(bool, camera_rotation_enabled, true, "group=Camera");
		AUTO_XML_VAR_TW(float, camera_rotation_speed, 10.0f, "precision=2 min=-30 max=30 step=5 group=Camera");

		AUTO_XML_VAR_TW(bool, simulation_paused, false, "group=Simulation");
		AUTO_XML_VAR_TW(int, solver_iterations, 20, "min=5 max=50 step=5 group=Simulation");
		AUTO_XML_VAR_TW(int, resolution_x, 128, "min=16 max=256 step=16 group=Simulation");
		AUTO_XML_VAR_TW(int, resolution_y, 128, "min=16 max=256 step=16 group=Simulation");
		AUTO_XML_VAR_TW(int, resolution_z, 128, "min=16 max=256 step=16 group=Simulation");

		AUTO_XML_VAR_TW(bool, obstacle_enabled, false, "group=Obstacle");
		AUTO_XML_VAR_TW(int, obstacle_type, 0, "min=0 max=1 step=1 group=Obstacle");
		AUTO_XML_VAR_TW(glm::vec3, obstacle_position, glm::vec3(0.5f,0.5f,0.5f), "group=Obstacle");
		AUTO_XML_VAR_TW(float, obstacle_radius, 0.3f, "precision=2 min=0 max=1 step=0.05 group=Obstacle");

		AUTO_XML_VAR_TW(float, velocity_dissipation_log10, -3.0f, "precision=2 min=-5 max=-1 step=0.25 group=Advection");
		AUTO_XML_VAR_TW(float, temperature_dissipation_log10, -3.0f, "precision=2 min=-5 max=-1 step=0.25 group=Advection");
		AUTO_XML_VAR_TW(float, temperature_decay_log10, -1.5f, "precision=2 min=-5 max=1 step=0.25 group=Advection");
		AUTO_XML_VAR_TW(float, density_dissipation_log10, -3.0f, "precision=2 min=-5 max=-1 step=0.25 group=Advection");
		AUTO_XML_VAR_TW(float, density_decay_log10, -1.5f, "precision=2 min=-5 max=1 step=0.25 group=Advection");

		AUTO_XML_VAR_TW(bool, injection_enabled, true, "group=Injection");
		AUTO_XML_VAR_TW(int, injection_count, 4, "min=1 max=12 step=1 group=Injection");
		AUTO_XML_VAR_TW(glm::vec3, injection_reference_position, glm::vec3(0.5f, 0.1f, 0.5f), "group=Injection");
		AUTO_XML_VAR_TW(float, injection_reference_distance, 0.25f, "precision=2 min=0 max=0.5 step=0.05 group=Injection");
		AUTO_XML_VAR_TW(bool, injection_linear_foce, false, "group=Injection");
		AUTO_XML_VAR_TW(float, injection_x_rotation, 30.0f, "precision=2 min=0 max=180 step=5 group=Injection");
		AUTO_XML_VAR_TW(bool, injection_y_rotation_enabled, false, "group=Injection");
		AUTO_XML_VAR_TW(float, injection_y_rotation_speed, 60.0f, "precision=2 min=-180 max=180 step=10 group=Injection");

		AUTO_XML_VAR_TW(float, injection_force_sigma, 0.25f, "precision=2 min=0.1 max=2 step=0.05 group=Injection");
		AUTO_XML_VAR_TW(float, injection_force_intensity_log10, 2.0f, "precision=2 min=-2 max=4 step=0.25 group=Injection");
		AUTO_XML_VAR_TW(float, injection_temperature_sigma, 0.5f, "precision=2 min=0.1 max=2 step=0.05 group=Injection");
		AUTO_XML_VAR_TW(float, injection_temperature_intensity_log10, 1.5f, "precision=2 min=0 max=4 step=0.25 group=Injection");
		AUTO_XML_VAR_TW(float, injection_density_sigma, 1.0f, "precision=2 min=0.1 max=2 step=0.05 group=Injection");
		AUTO_XML_VAR_TW(float, injection_density_intensity_log10, 2.0f, "precision=2 min=0 max=4 step=0.25 group=Injection");
		AUTO_XML_VAR_TW(bool, injection_density_color_random_hue, false, "group=Injection");
		AUTO_XML_VAR_TW(float, injection_density_color_hue, 45.0f, "precision=2 min=0 max=360 step=10 group=Injection");
		AUTO_XML_VAR_TW(float, injection_density_color_saturation, 1.0f, "precision=2 min=0 max=1 step=0.05 group=Injection");
		AUTO_XML_VAR_TW(float, injection_density_color_value, 1.0f, "precision=2 min=0 max=1 step=0.05 group=Injection");

		AUTO_XML_VAR_TW(bool, explosion_enabled, false, "group=Explosion");
		AUTO_XML_VAR_TW(glm::vec3, explosion_position, glm::vec3(0.5f, 0.1f, 0.5f), "group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_timer, 3.0f, "precision=2 min=0.1 max=10 step=0.5 group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_force_sigma, 0.25f, "precision=2 min=0.1 max=2 step=0.05 group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_force_intensity_log10, 2.0f, "precision=2 min=-2 max=4 step=0.25 group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_temperature_sigma, 0.25f, "precision=2 min=0.1 max=2 step=0.05 group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_temperature_intensity_log10, 1.25f, "precision=2 min=0 max=4 step=0.25 group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_density_sigma, 0.55f, "precision=2 min=0.1 max=2 step=0.05 group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_density_intensity_log10, 2.0f, "precision=2 min=0 max=4 step=0.25 group=Explosion");
		AUTO_XML_VAR_TW(bool, explosion_density_color_random_hue, true, "group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_density_color_hue, 45.0f, "precision=2 min=0 max=360 step=10 group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_density_color_saturation, 1.0f, "precision=2 min=0 max=1 step=0.05 group=Explosion");
		AUTO_XML_VAR_TW(float, explosion_density_color_value, 1.0f, "precision=2 min=0 max=1 step=0.05 group=Explosion");
		

		AUTO_XML_VAR_TW(bool, buoyancy_enabled, true, "group=Buoyancy");
		AUTO_XML_VAR_TW(float, ambient_temperature, 0.0f, "precision=2 min=-5 max=10 step=0.5 group=Buoyancy");
		AUTO_XML_VAR_TW(float, buoyancy_log10, 1.0f, "precision=2 min=0 max=5 step=0.25 group=Buoyancy");
		AUTO_XML_VAR_TW(float, weight_log10, 1.0f, "precision=2 min=0 max=5 step=0.25 group=Buoyancy");

		AUTO_XML_VAR_TW(bool, vorticity_confinement_enabled, true, "group=Vorticity");
		AUTO_XML_VAR_TW(float, vorticity_confinement_strength, 10.0f, "precision=2 min=0 max=100 step=2.5 group=Vorticity");

		AUTO_XML_VAR_TW(float, gradient_scale, 1.1f, "precision=2 min=1 max=2 step=0.05 group=Projection");

		AUTO_XML_VAR_TW(float, smoke_color_absorption_log10, 2.0f, "precision=2 min=-1 max=3 step=0.25 group=Smoke");
		AUTO_XML_VAR_TW(float, smoke_density_factor_log10, 1.0f, "precision=2 min=-1 max=3 step=0.25 group=Smoke");
		AUTO_XML_VAR_TW(int, smoke_sampling_resolution, 128, "min=16 max=256 step=16 group=Smoke");
		AUTO_XML_VAR_TW(float, smoke_sampling_jittering, 1.0f, "precision=2 min=0 max=1 step=0.1 group=Smoke");
		AUTO_XML_VAR_TW(bool, smoke_blur_enabled, true, "group=Smoke");
		AUTO_XML_VAR_TW(float, smoke_blur_sigma, 0.75f, "precision=2 min=0.1 max=2 step=0.05 group=Smoke");
		

		AUTO_XML_VAR_TW(glm::vec3, light_position, glm::vec3(4.0, 1.0, 2.0), "group=Lighting");
		AUTO_XML_VAR_TW(AColor3F, light_color, AColor3F{ glm::vec3(1.0f, 1.0f, 1.0f) }, "group=Lighting");
		AUTO_XML_VAR_TW(float, light_absorption_log10, 2.0f, "precision=2 min=-1 max=3 step=0.25 group=Lighting");
		AUTO_XML_VAR_TW(float, light_intensity_log10, 2.0f, "precision=2 min=-1 max=3 step=0.25 group=Lighting");
		AUTO_XML_VAR_TW(float, ambient_light_log10, 0.0f, "precision=2 min=-2 max=2 step=0.25 group=Lighting");

		AUTO_XML_VAR_TW(bool, shadows_enabled, true, "group=Shadows");
		AUTO_XML_VAR_TW(int, shadows_sampling_resolution, 64, "min=16 max=256 step=16 group=Shadows");
		AUTO_XML_VAR_TW(float, shadows_sampling_jittering, 0.5f, "precision=2 min=0 max=1 step=0.1 group=Shadows");
		AUTO_XML_VAR_TW(bool, shadows_blur_enabled, true, "group=Shadows");
		AUTO_XML_VAR_TW(float, shadows_blur_sigma, 1.5f, "precision=2 min=0.1 max=2 step=0.05 group=Shadows");

		AUTO_XML_VAR_TW(bool, radiance_color_enabled, false, "group=Radiance");
		AUTO_XML_VAR_TW(float, radiance_color_falloff_log10, 2.0f, "precision=2 min=-1 max=5 step=0.25 group=Radiance");

	};

}



