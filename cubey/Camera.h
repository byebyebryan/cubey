#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "Event.h"
#include "Engine.h"

namespace cubey {

	class Camera {
	public:
		Camera();

		static Camera* Main() {
			static Camera* instance = new Camera();
			return instance;
		}

		void Init();
		void Reset();
		void Update(float delta_time);

		glm::mat4 GetProjectionMat();
		glm::mat4 GetViewMat();
		glm::mat4 GetMVPMat(const glm::mat4& model_mat);

		float fov_;
		float aspect_;
		float near_;
		float far_;

		float mouse_sensitivty_;
		float movement_speed_;

		glm::mat4 transform_mat_;
		glm::vec3 position_;
		glm::quat orientation_;
		glm::vec3 look_at_;

		glm::vec3 forward_;
		glm::vec3 right_;
		glm::vec3 up_;
	private:
		EventLisenter<Engine::UpdateEvent> update_listener_;

		void UpdateTransform();
	};
}