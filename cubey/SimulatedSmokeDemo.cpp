#include "SimulatedSmokeDemo.h"

#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

#define GLOBAL_SIZE_X 64
#define GLOBAL_SIZE_Y 64
#define GLOBAL_SIZE_Z 64

#define WORKGROUP_COUNT_X GLOBAL_SIZE_X/LOCAL_WORKGROUP_SIZE_X
#define WORKGROUP_COUNT_Y GLOBAL_SIZE_Y/LOCAL_WORKGROUP_SIZE_Y
#define WORKGROUP_COUNT_Z GLOBAL_SIZE_Z/LOCAL_WORKGROUP_SIZE_Z

namespace cubey {

	void SimulatedSmokeDemo::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_TEXTURE_3D);
		glClearColor(0.5, 0.5, 0.5, 1.0);
		//glPointSize(2.5f);

		init_fill_rgba_ = new ShaderProgram();
		init_fill_rgba_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _FILL_RGBA_");
		init_fill_rgba_->Link();

		init_fill_r_ = new ShaderProgram();
		init_fill_r_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _FILL_R_");
		init_fill_r_->Link();

		init_fill_obstacle_ = new ShaderProgram();
		init_fill_obstacle_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _FILL_OBSTACLE_");
		init_fill_obstacle_->Link();

		update_advect_rgba_ = new ShaderProgram();
		update_advect_rgba_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _ADVECT_RGBA_");
		update_advect_rgba_->Link();

		update_advect_r_ = new ShaderProgram();
		update_advect_r_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _ADVECT_R_");
		update_advect_r_->Link();

		update_advect_r_mac_cormack_ = new ShaderProgram();
		update_advect_r_mac_cormack_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _ADVECT_R_MAC_CORMACK_");
		update_advect_r_mac_cormack_->Link();

		update_splat_ = new ShaderProgram();
		update_splat_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _SPLAT_");
		update_splat_->Link();

		update_buoyancy_ = new ShaderProgram();
		update_buoyancy_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _APPLY_BUOYANCY_");
		update_buoyancy_->Link();

		update_vorticity_ = new ShaderProgram();
		update_vorticity_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _COMPUTE_VORTICITY_");
		update_vorticity_->Link();

		update_confinement_ = new ShaderProgram();
		update_confinement_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _COMPUTE_CONFINEMENT_");
		update_confinement_->Link();

		update_divergence_ = new ShaderProgram();
		update_divergence_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _COMPUTE_DIVERGENCE_");
		update_divergence_->Link();

		update_jacobi_ = new ShaderProgram();
		update_jacobi_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _JACOBI_");
		update_jacobi_->Link();

		update_gradient_ = new ShaderProgram();
		update_gradient_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_update_shader.glsl", "#define _SUBTRACT_GRADIENT_");
		update_gradient_->Link();

		init_fill_verts_ = new ShaderProgram();
		init_fill_verts_->AddShader(GL_COMPUTE_SHADER, "shaders\\smoke_simulation_render_shader.glsl", "#define _VERTEX_INIT_");
		init_fill_verts_->Link();

		render_ = new ShaderProgram();
		render_->AddShader(GL_VERTEX_SHADER, "shaders\\smoke_simulation_render_shader.glsl", "#define _VERTEX_S_");
		render_->AddShader(GL_FRAGMENT_SHADER, "shaders\\smoke_simulation_render_shader.glsl", "#define _FRAGMENT_S_");
		render_->Link();

		render_first_pass_ = new ShaderProgram();
		render_first_pass_->AddShader(GL_VERTEX_SHADER, "shaders\\smoke_simulation_raytracing_shader.glsl", "#define _FIRST_PASS_VERTEX_S_");
		render_first_pass_->AddShader(GL_FRAGMENT_SHADER, "shaders\\smoke_simulation_raytracing_shader.glsl", "#define _FIRST_PASS_FRAGMENT_S_");
		render_first_pass_->Link();

		render_second_pass_ = new ShaderProgram();
		render_second_pass_->AddShader(GL_VERTEX_SHADER, "shaders\\smoke_simulation_raytracing_shader.glsl", "#define _SECOND_PASS_VERTEX_S_");
		render_second_pass_->AddShader(GL_FRAGMENT_SHADER, "shaders\\smoke_simulation_raytracing_shader.glsl", "#define _SECOND_PASS_FRAGMENT_S_");
		render_second_pass_->Link();

		render_debug_ = new ShaderProgram();
		render_debug_->AddShader(GL_VERTEX_SHADER, "shaders\\debug_shader_unlit.glsl", "#define _VERTEX_S_");
		render_debug_->AddShader(GL_FRAGMENT_SHADER, "shaders\\debug_shader_unlit.glsl", "#define _FRAGMENT_S_");
		render_debug_->Link();

		/*unsigned long long i = 0;
		glm::vec3* data = new glm::vec3[GLOBAL_SIZE_X * GLOBAL_SIZE_Y * GLOBAL_SIZE_Z];
		for (int x = 0; x < GLOBAL_SIZE_X; x++) {
		for (int y = 0; y < GLOBAL_SIZE_Y; y++) {
		for (int z = 0; z < GLOBAL_SIZE_Z; z++) {
		data[i] = glm::vec3(x / 10.0f, y / 10.0f, z / 10.0f);
		i++;
		}
		}
		}

		glGenBuffers(1, &ssbo_);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 3 * sizeof(GLfloat) * GLOBAL_SIZE_X * GLOBAL_SIZE_Y * GLOBAL_SIZE_Z, data, GL_DYNAMIC_COPY);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_);

		delete[] data;
		*/
		//init_fill_verts_->Activate();

		//glDispatchCompute(WORK_GROUP_NUM, WORK_GROUP_NUM, WORK_GROUP_NUM);
		//glMemoryBarrier(GL_ALL_BARRIER_BITS);

		GenTexture(i_velocity, GL_RGBA16F);
		GenTexture(i_temperature, GL_R16F);
		GenTexture(i_density, GL_R16F);
		GenTexture(i_pressure, GL_R16F);
		GenTexture(i_divergence, GL_R16F);
		GenTexture(i_obstacle, GL_RGBA16F);
		GenTexture(i_vorticity, GL_RGBA16F);

		GenTexture(i_phi_n_1_hat, GL_R16F);
		GenTexture(i_phi_n_hat, GL_R16F);

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

		init_fill_obstacle_->Activate();
		init_fill_obstacle_->SetUniform("u_location", glm::vec3(GLOBAL_SIZE_X / 2.0f, GLOBAL_SIZE_Y / 2.0f, GLOBAL_SIZE_Z / 2.0f));
		init_fill_obstacle_->SetUniform("u_radius", 0.0f);

		glBindImageTexture(0, i_obstacle, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		/*GLint view_port[4];
		glGetIntegerv(GL_VIEWPORT, view_port);

		glGenTextures(1, &t_entry_points);
		glBindTexture(GL_TEXTURE_2D, t_entry_points);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, view_port[2], view_port[3]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

		glGenTextures(1, &t_exit_points);
		glBindTexture(GL_TEXTURE_2D, t_exit_points);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, view_port[2], view_port[3]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

		glGenFramebuffers(1, &fb_entry_points);
		glGenFramebuffers(1, &fb_exit_points);

		auto verts = PrimitiveFactory::UnitBoxUnlit();
		for (auto & vertex : verts) {
			vertex.attrib_1 = vertex.attrib_0 + glm::vec3(0.5f);
		}*/

		mesh = Mesh::Create(PrimitiveFactory::UnitBoxUnlit(), GL_TRIANGLES);
		mesh_instance = mesh->CreateSimpleInstance(render_debug_, "u_mvp_mat");

		glm::vec2 fsq_verts[6] = {
			{ -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f }
		};

		GLuint vbo;
		glGenBuffers(1, &vbo);
		glGenVertexArrays(1, &fullscreen_quad_vao);

		glBindVertexArray(fullscreen_quad_vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec2) * 6, fsq_verts, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, (GLvoid*)0);
		glEnableVertexAttribArray(0);

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		Camera::Main()->transform_.TranslateTo(glm::vec3(0, 0, -1.5f));
	}

	void SimulatedSmokeDemo::Update(float delta_time) {
		update_advect_rgba_->Activate();
		update_advect_rgba_->SetUniform("u_time_step", delta_time);
		update_advect_rgba_->SetUniform("u_dissipation", 0.99f);

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(3, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();

		update_advect_r_->Activate();
		update_advect_r_->SetUniform("u_time_step", delta_time);
		update_advect_r_->SetUniform("u_dissipation", 0.99f);

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_temperature.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_temperature.Swap();

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
		update_advect_r_mac_cormack_->SetUniform("u_dissipation", 0.99f);

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_density.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glBindImageTexture(4, i_phi_n_1_hat, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(5, i_phi_n_hat, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_density.Swap();

		update_splat_->Activate();
		update_splat_->SetUniform("u_time_step", delta_time);
		update_splat_->SetUniform("u_location", glm::vec3(GLOBAL_SIZE_X / 2.0f, GLOBAL_SIZE_Y / 8.0f, GLOBAL_SIZE_Z / 2.0f));
		update_splat_->SetUniform("u_radius", 0.4f);
		update_splat_->SetUniform("u_intensity", 100.0f);

		glBindImageTexture(0, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(1, i_temperature.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_temperature.Swap();

		update_splat_->SetUniform("u_radius", 0.8f);
		update_splat_->SetUniform("u_intensity", 10.0f);

		glBindImageTexture(0, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(1, i_density.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_density.Swap();

		update_buoyancy_->Activate();
		update_buoyancy_->SetUniform("u_ambient_temperature", 0.0f);
		update_buoyancy_->SetUniform("u_time_step", delta_time);
		update_buoyancy_->SetUniform("u_buoyancy", 10.0f);
		update_buoyancy_->SetUniform("u_weight", 0.125f);

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_temperature.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(2, i_density.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();

		update_vorticity_->Activate();

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_vorticity, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		update_confinement_->Activate();
		update_confinement_->SetUniform("u_time_step", delta_time);
		update_confinement_->SetUniform("u_vorticity_strength", 10.0f);

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_vorticity, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();

		update_divergence_->Activate();

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, i_divergence, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		init_fill_r_->Activate();

		glBindImageTexture(0, i_pressure.ping, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		update_jacobi_->Activate();

		glBindImageTexture(0, i_divergence, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);

		for (int i = 0; i < 20; i++) {
			glBindImageTexture(2, i_pressure.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
			glBindImageTexture(3, i_pressure.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

			glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);

			i_pressure.Swap();
		}
		
		update_gradient_->Activate();

		glBindImageTexture(0, i_velocity.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, i_obstacle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, i_pressure.ping, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
		glBindImageTexture(3, i_velocity.pong, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

		glDispatchCompute(WORKGROUP_COUNT_X, WORKGROUP_COUNT_Y, WORKGROUP_COUNT_Z);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		i_velocity.Swap();

		Camera::Main()->Orbit(delta_time * glm::radians(15.0f), 0);
	}

	void SimulatedSmokeDemo::Render() {
		/*render_->Activate();
		render_->SetUniform("u_mvp_mat", Camera::Main()->CalculateMVPMat(glm::mat4()));
		render_->SetUniform("u_temp_dens", 0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, i_density.ping);

		glBindBuffer(GL_ARRAY_BUFFER, ssbo_);
		glVertexAttribPointer(0, 3, GL_FLOAT, false, 0, (GLvoid*)0);
		glEnableVertexAttribArray(0);
		glDrawArrays(GL_POINTS, 0, GLOBAL_SIZE_X * GLOBAL_SIZE_Y * GLOBAL_SIZE_Z);
		glBindBuffer(GL_ARRAY_BUFFER, 0);*/

		/*render_first_pass_->Activate();
		glEnable(GL_DEPTH_TEST);
		glCullFace(GL_BACK);
		glBindFramebuffer(GL_FRAMEBUFFER, fb_entry_points);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t_entry_points, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		mesh_instance->Draw();

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glCullFace(GL_FRONT);
		glBindFramebuffer(GL_FRAMEBUFFER, fb_exit_points);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t_exit_points, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		mesh_instance->Draw();

		glCullFace(GL_BACK);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);*/

		//glDisable(GL_DEPTH_TEST);

		//render_debug_->Activate();
		//mesh_instance->Draw();

		render_second_pass_->Activate();

		GLint viewport_size[4];
		glGetIntegerv(GL_VIEWPORT, viewport_size);

		glm::vec2 viewport_size_f = { viewport_size[2], viewport_size[3] };

		render_second_pass_->SetUniform("u_viewport_size", viewport_size_f);
		render_second_pass_->SetUniform("u_inverse_mvp", glm::inverse(Camera::Main()->view_mat()));

		//glBindImageTexture(0, t_entry_points, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
		//glBindImageTexture(1, t_exit_points, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, i_density.ping);

		glBindVertexArray(fullscreen_quad_vao);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);

		
	}

	void SimulatedSmokeDemo::GenTexture(GLuint& tex, GLenum internal_format) {
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

	void SimulatedSmokeDemo::GenTexture(Slab& slab, GLenum internal_format) {
		GenTexture(slab.ping, internal_format);
		GenTexture(slab.pong, internal_format);
	}

}
