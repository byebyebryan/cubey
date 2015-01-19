#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

namespace cubey {

	class Camera {
	public:
		static Camera* Main() {
			static Camera instance;
			return &instance;
		}

		void Reset();
		void Init();
		void Update(float delta_time);

		void MouseButtonHandler(int button, int action);
		void MouseWheelHandler(float yoffset);
		void KeyHandler(int key, int action);

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
		void UpdateTransform();

		bool left_mouse_btn_drag_engaged_;
		float left_mouse_btn_drag_prev_pos_x_;
		float left_mouse_btn_drag_prev_pos_y_;

		bool right_mouse_btn_drag_engaged_;
		float right_mouse_btn_drag_prev_pos_x_;
		float right_mouse_btn_drag_prev_pos_y_;

		glm::vec3 movement_;
	};
}