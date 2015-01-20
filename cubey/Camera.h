#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "Event.h"
#include "Engine.h"
#include "Transform.h"

namespace cubey {

	class Camera : public Transform {
	public:
		Camera();

		static Camera* Main() {
			static Camera* instance = new Camera();
			return instance;
		}

		void Init();
		void Reset();
		void Update(float delta_time);

		glm::mat4 ProjectionMat();
		glm::mat4 ViewMat();
		glm::mat4 MVPMat(const glm::mat4& model_mat);
		glm::mat3 NormalMat(const glm::mat4& model_mat);

		float fov_;
		float aspect_;
		float z_near_;
		float z_far_;

		float mouse_sensitivty_;
		float movement_speed_;
	private:
		void UpdateTransform();

		Transform::ScaleTo;
		Transform::Scale;

		EventLisenter<Engine::UpdateEvent> update_listener_;

		glm::vec3 look_at_target_pos_;
		Transform* look_at_target_;
		glm::mat4 projection_mat_;
		glm::mat4 view_mat_;

		glm::mat4 flip_mat_;

		friend class UI;
	};
}