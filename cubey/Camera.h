#pragma once

#include "GL/glew.h"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

namespace cubey {

	class Camera {
	public:
		Camera();

		static Camera* Main();

		void Init();
		void Update(float delta_time);

		void MouseButtonHandler(int button, int action);
		void MouseWheelHandler(float yoffset);

		glm::mat4 GetProjectionMat();
		glm::mat4 GetViewMat();

		glm::mat4 transform_mat_;
		glm::vec3 position_;
		glm::quat orientation_;
		glm::vec3 look_at_;

		glm::vec3 forward_;
		glm::vec3 right_;
		glm::vec3 up_;
	private:
		void UpdateOrientation();

		float fov_;
		float aspect_;
		float near_;
		float far_;

		float mouse_drag_sensitivty_;

		bool right_mouse_btn_drag_engaged_;
		float right_mouse_btn_drag_prev_pos_x_;
		float right_mouse_btn_drag_prev_pos_y_;
	};

}