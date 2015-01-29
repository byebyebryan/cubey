#include "ParticleTest.h"

#include "glm/gtc/random.hpp"
#include "glm/gtx/quaternion.hpp"

#define MAX_PARTICLE_PACK_COUNT 10000
#define MAX_ATTRACTOR_COUNT 64 
#define MAX_STREAM_COUNT 128

#define DEFAULT_PARTICLE_PACK_COUNT 4000
#define DEFAULT_ATTRACTOR_COUNT 8
#define DEFAULT_STREAM_COUNT 32

#define DEFAULT_PARTICLE_LIFE 5.0f
#define DEFAULT_PARTICLE_STREAM_RATIO 1.0f
#define DEFAULT_PARTICLE_INIT_SPREAD 1.0f
#define DEFAULT_PARTICLE_INIT_SPEED 5.0f
#define DEFAULT_PARTICLE_BOUND 100.0f
#define DEFAULT_PARTICLE_STREAM_DEVIATION 0.2f
#define DEFAULT_PARTICLE_SPEED_DECAY 0.0f
#define DEFAULT_PARTICLE_SPEED_RAMDOMIZER 0.0f

#define DEFAULT_PARTICLE_SIZE 0.5f

#define DEFAULT_ATTRACTION_FORCE_MULTIPLIER -1.0f

#define DEFAULT_ATTRACTOR_MIN_MASS 200.0f
#define DEFAULT_ATTRACTOR_MAX_MASS 500.0f
#define DEFAULT_ATTRACTOR_MIN_DIST 10.0f
#define DEFAULT_ATTRACTOR_MAX_DIST 25.0f
#define DEFAULT_ATTRACTOR_MIN_TIME 5.0f
#define DEFAULT_ATTRACTOR_MAX_TIME 8.0f

#define DEFAULT_STREAM_MIN_VEL 5.0f
#define DEFAULT_STREAM_MAX_VEL 10.0f
#define DEFAULT_STREAM_MIN_TIME 2.5f
#define DEFAULT_STREAM_MAX_TIME 5.0f

#define DEFAULT_PARTICLE_COLOR_COLD glm::vec4(0.0f, 0.2f, 1.0f, 0.1f);
#define DEFAULT_PARTICLE_COLOR_HOT glm::vec4(1.0f, 0.2f, 0.0f, 0.1f);

#define DEFAULT_CAMERA_ORBIT_SPEED 15.0f

namespace cubey {
	void IOrbitalObject::Init() {
		SetNextDestination();
		travel_time_.current_value = travel_time_.destination_value;
	}

	void IOrbitalObject::SetNextDestination() {
		travel_time_.original_value = 0;
		travel_time_.destination_value = glm::linearRand(min_travel_time_, max_travel_time_);
		distance_to_center_.original_value = distance_to_center_.destination_value;
		distance_to_center_.destination_value = glm::linearRand(min_distance_to_center_, max_distance_to_center_);
		rotation_.original_value = rotation_.destination_value;
		rotation_.destination_value = glm::rotation(glm::vec3(0, 1.0f, 0), glm::sphericalRand(1.0f));
	}

	void IOrbitalObject::Update(float dt) {
		if (travel_time_.current_value >= travel_time_.destination_value) {
			travel_time_.current_value -= travel_time_.destination_value;
			SetNextDestination();
		}

		progress_ = travel_time_.current_value / travel_time_.destination_value;
		progress_smoothed_ = glm::smoothstep(0.0f, 1.0f, progress_);
		distance_to_center_.current_value = glm::mix(distance_to_center_.original_value, distance_to_center_.destination_value, progress_smoothed_);
		rotation_.current_value = glm::slerp(rotation_.original_value, rotation_.destination_value, progress_smoothed_);

		travel_time_.current_value += dt;
	}

	glm::vec3 IOrbitalObject::GetVec3() {
		return glm::vec3(0, 1.0f, 0) * rotation_.current_value * distance_to_center_.current_value;
	}

	void Attractor::Init() {
		SetNextDestination();
		travel_time_.current_value = travel_time_.destination_value;
	}

	void Attractor::SetNextDestination() {
		IOrbitalObject::SetNextDestination();
		mass_.original_value = mass_.destination_value;
		mass_.destination_value = glm::linearRand(min_mass_, max_mass_);
	}

	void Attractor::Update(float dt) {
		IOrbitalObject::Update(dt);
		mass_.current_value = glm::mix(mass_.original_value, mass_.destination_value, progress_smoothed_);
	}

	glm::vec4 Attractor::GetVec4() {
		return glm::vec4(GetVec3(), mass_.current_value);
	}

	void ParticleTest::Init() {
		//glEnable(GL_DEPTH_TEST);
		//glEnable(GL_CULL_FACE);
		//glClearColor(0.4, 0.4, 0.4, 1);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		u_particle_lifespan_ = DEFAULT_PARTICLE_LIFE;
		u_particle_stream_ratio_ = DEFAULT_PARTICLE_STREAM_RATIO;
		u_particle_initial_speed_ = DEFAULT_PARTICLE_INIT_SPEED;
		u_particle_initial_spread_ = DEFAULT_PARTICLE_INIT_SPREAD;
		u_particle_bound_ = DEFAULT_PARTICLE_BOUND;
		u_particle_stream_deviation_ = DEFAULT_PARTICLE_STREAM_DEVIATION;
		u_particle_speed_decay_ = DEFAULT_PARTICLE_SPEED_DECAY;
		u_particle_speed_randomizer_ = DEFAULT_PARTICLE_SPEED_RAMDOMIZER;

		u_particle_size_ = DEFAULT_PARTICLE_SIZE;

		u_particle_pack_count_ = DEFAULT_PARTICLE_PACK_COUNT;
		u_attractor_count_ = DEFAULT_ATTRACTOR_COUNT;
		u_stream_count_ = DEFAULT_STREAM_COUNT;

		u_attraction_force_multiplier_ = DEFAULT_ATTRACTION_FORCE_MULTIPLIER;

		u_particle_color_cold_ = DEFAULT_PARTICLE_COLOR_COLD;
		u_particle_color_hot_ = DEFAULT_PARTICLE_COLOR_HOT;

		//u_particle_hue_ = 0.0f;

		attractors_min_distance_ = DEFAULT_ATTRACTOR_MIN_DIST;
		attractors_max_distance_ = DEFAULT_ATTRACTOR_MAX_DIST;
		attractors_min_mass_ = DEFAULT_ATTRACTOR_MIN_MASS;
		attractors_max_mass_ = DEFAULT_ATTRACTOR_MAX_MASS;
		attractors_min_travel_time_ = DEFAULT_ATTRACTOR_MIN_TIME;
		attractors_max_travel_time_ = DEFAULT_ATTRACTOR_MAX_TIME;

		streams_min_vel_ = DEFAULT_STREAM_MIN_VEL;
		streams_max_vel_ = DEFAULT_STREAM_MAX_VEL;
		streams_min_travel_time_ = DEFAULT_STREAM_MIN_TIME;
		streams_max_travel_time_ = DEFAULT_STREAM_MAX_TIME;

		particle_count = (unsigned long long)u_particle_pack_count_ * 128;

		camera_orbit_speed_ = DEFAULT_CAMERA_ORBIT_SPEED;

		/*debug_prog_ = new ShaderProgram();
		debug_prog_->AddShader(GL_VERTEX_SHADER, "shaders\\debug_shader_unlit.glsl", "#define _VERTEX_S_");
		debug_prog_->AddShader(GL_FRAGMENT_SHADER, "shaders\\debug_shader_unlit.glsl", "#define _FRAGMENT_S_");
		debug_prog_->Link();*/

		init_prog_ = new ShaderProgram();
		init_prog_->AddShader(GL_COMPUTE_SHADER, "shaders\\particle_init_shader.glsl");
		init_prog_->Link();

		update_prog_ = new ShaderProgram();
		update_prog_->AddShader(GL_COMPUTE_SHADER, "shaders\\particle_update_shader.glsl");
		update_prog_->Link();

		render_prog_ = new ShaderProgram();
		render_prog_->AddShader(GL_VERTEX_SHADER, "shaders\\particle_render_shader.glsl", "#define _VERTEX_S_");
		render_prog_->AddShader(GL_GEOMETRY_SHADER, "shaders\\particle_render_shader.glsl", "#define _GEOMETRY_S_");
		render_prog_->AddShader(GL_FRAGMENT_SHADER, "shaders\\particle_render_shader.glsl", "#define _FRAGMENT_S_");
		render_prog_->Link();

		/*glm::vec4* particle_data = new glm::vec4[MAX_PARTICLE_PACK_COUNT * 128 * 2];
		for (int i = 0; i < MAX_PARTICLE_PACK_COUNT * 128; i++) {
		particle_data[i * 2] = glm::vec4(glm::ballRand(u_particle_initial_spread_), glm::linearRand(0.0f, u_particle_lifespan_));
		particle_data[i * 2 + 1] = glm::vec4(glm::ballRand(u_particle_initial_speed_), glm::linearRand(0.0f, 1.0f));
		}*/

		glGenBuffers(1, &ssbo_);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(glm::vec4) * MAX_PARTICLE_PACK_COUNT * 128 * 2, NULL, GL_DYNAMIC_COPY);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_);

		init_prog_->Activate();
		init_prog_->SetUniform("u_particle_lifespan", u_particle_lifespan_);
		init_prog_->SetUniform("u_particle_initial_spread", u_particle_initial_spread_);
		init_prog_->SetUniform("u_particle_initial_speed", u_particle_initial_speed_);
		init_prog_->SetUniform("u_random_seed", (float)Time::time_since_start());

		glDispatchCompute(MAX_PARTICLE_PACK_COUNT, 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		//delete[] particle_data;

		for (int i = 0; i < MAX_ATTRACTOR_COUNT; i++) {
			attractors_.push_back(Attractor(DEFAULT_ATTRACTOR_MIN_DIST, DEFAULT_ATTRACTOR_MAX_DIST, 
				DEFAULT_ATTRACTOR_MIN_TIME, DEFAULT_ATTRACTOR_MAX_TIME,
				DEFAULT_ATTRACTOR_MIN_MASS, DEFAULT_ATTRACTOR_MAX_MASS));
			attractors_[i].Init();
			u_attractors_.push_back(attractors_[i].GetVec4());
		}

		for (int i = 0; i < MAX_STREAM_COUNT; i++) {
			streams_.push_back(Stream(DEFAULT_STREAM_MIN_VEL, DEFAULT_STREAM_MAX_VEL, DEFAULT_STREAM_MIN_TIME, DEFAULT_STREAM_MAX_TIME));
			streams_[i].Init();
			u_streams_.push_back(streams_[i].GetVec3());
		}

		TwAddVarRW(UI::Main()->tw_bar_, "particle count", TW_TYPE_UINT32, &particle_count, "");

		TwAddVarRW(UI::Main()->tw_bar_, "particle group count", TW_TYPE_UINT16, &u_particle_pack_count_, "min=100 max=10000 step=1000");
		TwAddVarRW(UI::Main()->tw_bar_, "attractor count", TW_TYPE_INT16, &u_attractor_count_, "min=0 max=64 step=4");
		TwAddVarRW(UI::Main()->tw_bar_, "stream count", TW_TYPE_INT16, &u_stream_count_, "min=0 max=128 step=4");

		TwAddVarRW(UI::Main()->tw_bar_, "particle lifespan", TW_TYPE_FLOAT, &u_particle_lifespan_, "min=1 max=25 step=1");
		TwAddVarRW(UI::Main()->tw_bar_, "particle bound", TW_TYPE_FLOAT, &u_particle_bound_, "min=5 max=125 step=5");
		TwAddVarRW(UI::Main()->tw_bar_, "attraction force log10", TW_TYPE_FLOAT, &u_attraction_force_multiplier_, "min=-8 max=8 step=1");

		TwAddVarRW(UI::Main()->tw_bar_, "particle initial spread", TW_TYPE_FLOAT, &u_particle_initial_spread_, "min=0 max=5 step=0.5");
		TwAddVarRW(UI::Main()->tw_bar_, "particle initial speed", TW_TYPE_FLOAT, &u_particle_initial_speed_, "min=0 max=10 step=1");

		TwAddVarRW(UI::Main()->tw_bar_, "particle stream ratio", TW_TYPE_FLOAT, &u_particle_stream_ratio_, "min=0 max=1 step=0.1");
		TwAddVarRW(UI::Main()->tw_bar_, "particle stream deviation", TW_TYPE_FLOAT, &u_particle_stream_deviation_, "min=0 max=1 step=0.1");
		TwAddVarRW(UI::Main()->tw_bar_, "particle speed decay", TW_TYPE_FLOAT, &u_particle_speed_decay_, "min=0 max=0.9 step=0.1");
		TwAddVarRW(UI::Main()->tw_bar_, "particle speed randomizer", TW_TYPE_FLOAT, &u_particle_speed_randomizer_, "min=0 max=1.0 step=0.05");
		
		TwAddVarRW(UI::Main()->tw_bar_, "particle size", TW_TYPE_FLOAT, &u_particle_size_, "min=0.05 max=1.0 step=0.05");

		//TwAddVarRW(UI::Main()->tw_bar_, "particle hue", TW_TYPE_FLOAT, &u_particle_hue_, "min=0 max=1.0 step=0.1");

		TwAddVarRW(UI::Main()->tw_bar_, "particle color cold", TW_TYPE_COLOR4F, &u_particle_color_cold_, "");
		TwAddVarRW(UI::Main()->tw_bar_, "particle color hot", TW_TYPE_COLOR4F, &u_particle_color_hot_, "");

		TwAddVarRW(UI::Main()->tw_bar_, "attractor distance min", TW_TYPE_FLOAT, &attractors_min_distance_, "min=5 max=50 step=5");
		TwAddVarRW(UI::Main()->tw_bar_, "attractor distance max", TW_TYPE_FLOAT, &attractors_max_distance_, "min=5 max=100 step=5");
		TwAddVarRW(UI::Main()->tw_bar_, "attractor mass min", TW_TYPE_FLOAT, &attractors_min_mass_, "min=-800 max=1600 step=100");
		TwAddVarRW(UI::Main()->tw_bar_, "attractor mass max", TW_TYPE_FLOAT, &attractors_max_mass_, "min=-800 max=1600 step=100");
		TwAddVarRW(UI::Main()->tw_bar_, "attractor travel time min", TW_TYPE_FLOAT, &attractors_min_travel_time_, "min=0.5 max=5.0 step=0.5");
		TwAddVarRW(UI::Main()->tw_bar_, "attractor travel time max", TW_TYPE_FLOAT, &attractors_max_travel_time_, "min=0.5 max=10.0 step=0.5");

		TwAddVarRW(UI::Main()->tw_bar_, "stream velocity min", TW_TYPE_FLOAT, &streams_min_vel_, "min=0.5 max=25 step=2.5");
		TwAddVarRW(UI::Main()->tw_bar_, "stream velocity max", TW_TYPE_FLOAT, &streams_max_vel_, "min=0.5 max=50 step=2.5");
		TwAddVarRW(UI::Main()->tw_bar_, "stream travel time min", TW_TYPE_FLOAT, &streams_min_travel_time_, "min=0.1 max=10.0 step=0.5");
		TwAddVarRW(UI::Main()->tw_bar_, "stream travel time max", TW_TYPE_FLOAT, &streams_max_travel_time_, "min=0.5 max=20.0 step=0.5");

		TwAddVarRW(UI::Main()->tw_bar_, "camera orbit speed", TW_TYPE_FLOAT, &camera_orbit_speed_, "min=0 max=60 step=5");

		update_prog_->Activate();
		update_prog_->SetUniform("u_particle_lifespan", u_particle_lifespan_);
		update_prog_->SetUniform("u_particle_stream_ratio", u_particle_stream_ratio_);
		update_prog_->SetUniform("u_particle_initial_spread", u_particle_initial_spread_);
		update_prog_->SetUniform("u_particle_initial_speed", u_particle_initial_speed_);
		update_prog_->SetUniform("u_particle_bound_sq", u_particle_bound_ * u_particle_bound_);
		update_prog_->SetUniform("u_particle_stream_deviation", u_particle_stream_deviation_);
		update_prog_->SetUniform("u_particle_speed_decay", u_particle_speed_decay_);
		update_prog_->SetUniform("u_particle_pack_count", u_particle_pack_count_);
		update_prog_->SetUniform("u_attractor_count", u_attractor_count_);
		update_prog_->SetUniform("u_stream_count", u_stream_count_);
		update_prog_->SetUniform("u_attraction_force_multiplier", glm::pow(10, u_attraction_force_multiplier_));

		render_prog_->Activate();
		render_prog_->SetUniform("u_particle_lifespan", u_particle_lifespan_);
		render_prog_->SetUniform("u_particle_color_cold", u_particle_color_cold_);
		render_prog_->SetUniform("u_particle_color_hot", u_particle_color_hot_);

		Camera::Main()->transform_.TranslateTo(glm::vec3(0, 0, -75.0f));
		//glPointSize(2.0f);
	}

	void ParticleTest::Update(float delta_time) {
		particle_count = (unsigned long long)u_particle_pack_count_ * 128;

		Camera::Main()->Orbit(delta_time * glm::radians(camera_orbit_speed_), 0);
		//u_particle_hue_ = 0.5f + 0.5f * glm::sin(Time::time_since_start() / 5.0f);

		for (int i = 0; i < MAX_ATTRACTOR_COUNT; i++) {
			attractors_[i].min_distance_to_center_ = attractors_min_distance_;
			attractors_[i].max_distance_to_center_ = attractors_max_distance_;

			attractors_[i].min_mass_ = attractors_min_mass_;
			attractors_[i].max_mass_ = attractors_max_mass_;

			attractors_[i].min_travel_time_ = attractors_min_travel_time_;
			attractors_[i].max_travel_time_ = attractors_max_travel_time_;

			attractors_[i].Update(delta_time);
			u_attractors_[i] = attractors_[i].GetVec4();
		}

		for (int i = 0; i < MAX_STREAM_COUNT; i++) {
			streams_[i].min_distance_to_center_ = streams_min_vel_;
			streams_[i].max_distance_to_center_ = streams_max_vel_;

			streams_[i].min_travel_time_ = streams_min_travel_time_;
			streams_[i].max_travel_time_ = streams_max_travel_time_;

			streams_[i].Update(delta_time);
			u_streams_[i] = streams_[i].GetVec3();
		}

		update_prog_->Activate();

		update_prog_->SetUniform("u_particle_lifespan", u_particle_lifespan_);
		update_prog_->SetUniform("u_particle_stream_ratio", u_particle_stream_ratio_);
		update_prog_->SetUniform("u_particle_initial_spread", u_particle_initial_spread_);
		update_prog_->SetUniform("u_particle_initial_speed", u_particle_initial_speed_);
		update_prog_->SetUniform("u_particle_bound_sq", u_particle_bound_ * u_particle_bound_);
		update_prog_->SetUniform("u_particle_stream_deviation", u_particle_stream_deviation_);
		update_prog_->SetUniform("u_particle_speed_decay", u_particle_speed_decay_);
		update_prog_->SetUniform("u_particle_speed_randomizer", u_particle_speed_randomizer_);
		update_prog_->SetUniform("u_particle_pack_count", u_particle_pack_count_);
		update_prog_->SetUniform("u_attractor_count", u_attractor_count_);
		update_prog_->SetUniform("u_stream_count", u_stream_count_);
		update_prog_->SetUniform("u_attraction_force_multiplier", glm::pow(10, u_attraction_force_multiplier_));

		//update_prog_->SetUniform("u_particle_hue", u_particle_hue_);

		update_prog_->SetUniform("u_delta_time", delta_time);
		update_prog_->SetUniform("u_attractors[0]", u_attractors_);
		update_prog_->SetUniform("u_streams[0]", u_streams_);
		//glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
		glDispatchCompute(MAX_PARTICLE_PACK_COUNT, 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		//glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	void ParticleTest::Render() {
		render_prog_->Activate();
		render_prog_->SetUniform("u_particle_lifespan", u_particle_lifespan_);
		render_prog_->SetUniform("u_particle_color_cold", u_particle_color_cold_);
		render_prog_->SetUniform("u_particle_color_hot", u_particle_color_hot_);
		render_prog_->SetUniform("u_view_model_mat", Camera::Main()->view_mat());
		render_prog_->SetUniform("u_projection_mat", Camera::Main()->projection_mat());
		render_prog_->SetUniform("u_particle_size", u_particle_size_);

		glBindBuffer(GL_ARRAY_BUFFER, ssbo_);
		glVertexAttribPointer(0, 4, GL_FLOAT, false, 2 * sizeof(glm::vec4), (GLvoid*)0);
		glEnableVertexAttribArray(0);
		//glVertexAttribPointer(1, 1, GL_FLOAT, false, 2 * sizeof(glm::vec4), (GLvoid*)(7 * sizeof(float)));
		//glEnableVertexAttribArray(1);

		glDrawArrays(GL_POINTS, 0, u_particle_pack_count_ * 128);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	


	


	


	

}
