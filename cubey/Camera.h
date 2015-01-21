#pragma once

#include "Interfaces.h"
#include "Transform.h"

namespace cubey {

	class Camera : public ISingleton<Camera> {
	public:
		Camera();

		void Init();
		void Reset();
		void Update(float delta_time);

		glm::mat4 projection_mat() { return projection_mat_; }
		glm::mat4 view_mat() { return view_mat_; }

		glm::mat4 CalculateMVPMat(const glm::mat4& model_mat) {
			return projection_mat_ * view_mat_ * model_mat;
		}
		glm::mat3 CalculateNormalMat(const glm::mat4& model_mat) {
			return glm::inverseTranspose(glm::mat3(view_mat_ * model_mat));
		}

		void LookAt(glm::vec3 target_pos);

		void PanTilt(float yaw, float pitch);
		void Orbit(float yaw, float pitch);

		float fov_;
		float aspect_;
		float z_near_;
		float z_far_;

		float mouse_sensitivty_;
		float movement_speed_;

		float pitch_limit_;

		Transform transform_;
	private:
		EventLisenter<Engine::UpdateEvent> update_listener_;

		glm::vec3 look_at_target_pos_;
		Transform* look_at_target_;
		glm::mat4 projection_mat_;
		glm::mat4 view_mat_;

		glm::mat4 flip_mat_;

		float yaw_;
		float pitch_;
		float roll_;

		friend class UI;
	};
}