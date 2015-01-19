#include "Camera.h"

#include "glm/gtx/rotate_vector.hpp"
#include "Engine.h"

namespace cubey {
	const float kDefaultFOV = 60.0f;
	const float kDefaultAspect = 1.0f;
	const float kDefaultNear = 1.0f;
	const float kDefaultFar = 500.0f;
	const glm::vec3 kDefaultPosition = glm::vec3(0.0f, 0.0f, 10.0f);
	const glm::vec3 kDefaultLookAt = glm::vec3();

	const float kDefaultMouseDragSensitivity = 1.0f;

	Camera::Camera() {
		fov_ = glm::radians(kDefaultFOV);
		aspect_ = kDefaultAspect;
		near_ = kDefaultNear;
		far_ = kDefaultFar;

		position_ = kDefaultPosition;
		look_at_ = kDefaultLookAt;

		transform_mat_ = GetViewMat();
		UpdateOrientation();

		right_mouse_btn_drag_engaged_ = false;
		right_mouse_btn_drag_prev_pos_x_ = 0.0f;
		right_mouse_btn_drag_prev_pos_y_ = 0.0f;
		mouse_drag_sensitivty_ = kDefaultMouseDragSensitivity;
	}

	Camera* Camera::Main() {
		static Camera main_camera;
		return &main_camera;
	}

	void Camera::Init() {
		int w, h;
		glfwGetFramebufferSize(Engine::window_, &w, &h);
		aspect_ = w / (float)h;

		EventChannel<Engine::UpdateEvent>::Add([this](const Engine::UpdateEvent& e){
			Update(e.deltatime);
		});
		EventChannel<Engine::MouseButtonEvent>::Add([this](const Engine::MouseButtonEvent& e){
			MouseButtonHandler(e.button, e.action);
		});
		EventChannel<Engine::MouseWheelEvent>::Add([this](const Engine::MouseWheelEvent& e){
			MouseWheelHandler(e.yoffset);
		});
	}

	void Camera::Update(float delta_time) {
		if (right_mouse_btn_drag_engaged_) {
			double x, y;
			glfwGetCursorPos(Engine::window_, &x, &y);
			float x_offset = x - right_mouse_btn_drag_prev_pos_x_;
			float y_offset = y - right_mouse_btn_drag_prev_pos_y_;

			glm::vec3 from_target = position_ - look_at_;
			from_target = glm::rotate(from_target, - y_offset * delta_time * mouse_drag_sensitivty_, right_);
			from_target = glm::rotateY(from_target, - x_offset * delta_time * mouse_drag_sensitivty_);

			position_ = look_at_ + from_target;
			transform_mat_ = GetViewMat();
			UpdateOrientation();

			right_mouse_btn_drag_prev_pos_x_ = x;
			right_mouse_btn_drag_prev_pos_y_ = y;
		}
	}

	void Camera::MouseButtonHandler(int button, int action) {
		if (button == GLFW_MOUSE_BUTTON_RIGHT){
			if (action == GLFW_PRESS) {
				right_mouse_btn_drag_engaged_ = true;
				double x, y;
				glfwGetCursorPos(Engine::window_, &x, &y);
				right_mouse_btn_drag_prev_pos_x_ = x;
				right_mouse_btn_drag_prev_pos_y_ = y;
			}
			else if (action == GLFW_RELEASE) {
				right_mouse_btn_drag_engaged_ = false;
			}
		}
	}

	void Camera::MouseWheelHandler(float yoffset) {
		position_ -= forward_ * yoffset;
	}

	glm::mat4 Camera::GetProjectionMat() {
		return glm::perspective(fov_, aspect_, near_, far_);
	}

	glm::mat4 Camera::GetViewMat() {
		return glm::lookAt(position_, look_at_, glm::vec3(0.0f, 1.0f, 0.0f));
	}

	void Camera::UpdateOrientation() {
		orientation_ = glm::quat(transform_mat_);
		forward_ = glm::vec3(0.0f, 0.0f, 1.0f) * orientation_;
		right_ = glm::vec3(1.0f, 0.0f, 0.0f) * orientation_;
		up_ = glm::vec3(0.0f, 1.0f, 0.0f) * orientation_;
	}

}


