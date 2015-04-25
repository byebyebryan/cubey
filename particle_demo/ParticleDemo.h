#pragma once

#include "cubey.h"

namespace cubey {

	class IOrbitalObject {
	public:
		template<typename T>
		struct ProgressiveValue
		{
			T current_value;
			T original_value;
			T destination_value;
		};

		IOrbitalObject() : min_distance_to_center_(0), max_distance_to_center_(0), min_travel_time_(0), max_travel_time_(0) {
		}

		IOrbitalObject(float _min_distance_to_center, float _max_distance_to_center, float _min_travel_time, float _max_travel_time)
			: min_distance_to_center_(_min_distance_to_center), max_distance_to_center_(_max_distance_to_center), 
			min_travel_time_(_min_travel_time), max_travel_time_(_max_travel_time) {
		}

		virtual void Init();
		virtual void SetNextDestination();
		virtual void Update(float dt);
		glm::vec3 GetVec3();

		ProgressiveValue<float> distance_to_center_;
		float min_distance_to_center_;
		float max_distance_to_center_;
		ProgressiveValue<glm::quat> rotation_;
		ProgressiveValue<float> travel_time_;
		float min_travel_time_;
		float max_travel_time_;

		float progress_;
		float progress_smoothed_;
	};

	class Stream : public IOrbitalObject {
	public:
		Stream(float _min_distance_to_center, float _max_distance_to_center, float _min_travel_time, float _max_travel_time)
			: IOrbitalObject(_min_distance_to_center, _max_distance_to_center, _min_travel_time, _max_travel_time) {
		}
	};

	class Attractor : public IOrbitalObject {
	public:
		Attractor(float _min_distance_to_center, float _max_distance_to_center,
			float _min_travel_time, float _max_travel_time, float _min_mass, float _max_mass)
			: IOrbitalObject(_min_distance_to_center, _max_distance_to_center, _min_travel_time, _max_travel_time), min_mass_(_min_mass), max_mass_(_max_mass) {
		}

		void Init() override;
		void SetNextDestination() override;
		void Update(float dt) override;

		glm::vec4 GetVec4();

		ProgressiveValue<float> mass_;
		float min_mass_;
		float max_mass_;
	};

	class ParticleDemo : public EngineEventsBase {
	public:
		void Init() override;
		void Update(float delta_time) override;
		void Render() override;

	private:
		ShaderProgram* debug_prog_;
		ShaderProgram* init_prog_;
		ShaderProgram* update_prog_;
		ShaderProgram* render_prog_;

		GLuint ssbo_;

		std::vector<glm::vec4> u_attractors_;
		std::vector<glm::vec3> u_streams_;

		std::vector<Attractor> attractors_;
		std::vector<Stream> streams_;;

		IOrbitalObject center_;

		float u_particle_lifespan_;
		float u_particle_stream_ratio_;
		float u_particle_initial_spread_;
		float u_particle_initial_speed_;
		float u_particle_bound_;
		float u_particle_stream_deviation_;
		float u_particle_speed_decay_;
		float u_particle_speed_randomizer_;

		float u_particle_size_;

		float u_attraction_force_multiplier_;

		unsigned int u_particle_pack_count_;
		int u_attractor_count_;
		int u_stream_count_;

		unsigned long long particle_count;

		//float u_particle_hue_;

		glm::vec4 u_particle_color_cold_;
		glm::vec4 u_particle_color_hot_;

		float attractors_min_mass_;
		float attractors_max_mass_;
		float attractors_min_distance_;
		float attractors_max_distance_;
		float attractors_min_travel_time_;
		float attractors_max_travel_time_;

		float streams_min_vel_;
		float streams_max_vel_;
		float streams_min_travel_time_;
		float streams_max_travel_time_;

		float camera_orbit_speed_;

		bool particle_paused_;

		GLuint particle_texture_;
	};
}


