#include "Camera.h"

#include "glm/gtx/rotate_vector.hpp"
#include "Engine.h"

namespace cubey {
	const float kDefaultFOV = 60.0f;
	const float kDefaultNear = 1.0f;
	const float kDefaultFar = 500.0f;
	const glm::vec3 kDefaultPosition = glm::vec3(0.0f, 0.0f, 10.0f);
	const glm::vec3 kDefaultLookAt = glm::vec3();

	const float kDefaultMouseSensitivity = 1.0f;
	const float kDefaultMovementSpeed = 5.0f;

	void Camera::Reset() {
		fov_ = glm::radians(kDefaultFOV);
		near_ = kDefaultNear;
		far_ = kDefaultFar;

		int w, h;
		glfwGetFramebufferSize(Engine::window_, &w, &h);
		aspect_ = w / (float)h;

		position_ = kDefaultPosition;
		look_at_ = kDefaultLookAt;

		transform_mat_ = GetViewMat();
		UpdateTransform();

		right_mouse_btn_drag_engaged_ = false;
		right_mouse_btn_drag_prev_pos_x_ = 0.0f;
		right_mouse_btn_drag_prev_pos_y_ = 0.0f;

		left_mouse_btn_drag_engaged_ = false;
		left_mouse_btn_drag_prev_pos_x_ = 0.0f;
		left_mouse_btn_drag_prev_pos_y_ = 0.0f;

		movement_ = glm::vec3();

		mouse_sensitivty_ = kDefaultMouseSensitivity;
		movement_speed_ = kDefaultMovementSpeed;
	}

	void Camera::Init() {
		Reset();
		
		EventChannel<Engine::UpdateEvent>::Add([this](const Engine::UpdateEvent& e){
			Update(e.deltatime);
		});
		EventChannel<Engine::MouseButtonEvent>::Add([this](const Engine::MouseButtonEvent& e){
			MouseButtonHandler(e.button, e.action);
		});
		EventChannel<Engine::MouseWheelEvent>::Add([this](const Engine::MouseWheelEvent& e){
			MouseWheelHandler(e.yoffset);
		});
		EventChannel<Engine::KeyEvent>::Add([this](const Engine::KeyEvent& e){
			KeyHandler(e.key, e.action);
		});
	}

	void Camera::Update(float delta_time) {
		
		if (left_mouse_btn_drag_engaged_) {
			double x, y;
			glfwGetCursorPos(Engine::window_, &x, &y);
			float x_offset = x - left_mouse_btn_drag_prev_pos_x_;
			float y_offset = y - left_mouse_btn_drag_prev_pos_y_;

			glm::vec3 to_target = look_at_ - position_;
			to_target = glm::rotate(to_target, -y_offset * delta_time * mouse_sensitivty_, right_);
			to_target = glm::rotateY(to_target, -x_offset * delta_time * mouse_sensitivty_);

			look_at_ = position_ + to_target;
			UpdateTransform();

			left_mouse_btn_drag_prev_pos_x_ = x;
			left_mouse_btn_drag_prev_pos_y_ = y;
		}
		if (right_mouse_btn_drag_engaged_) {
			double x, y;
			glfwGetCursorPos(Engine::window_, &x, &y);
			float x_offset = x - right_mouse_btn_drag_prev_pos_x_;
			float y_offset = y - right_mouse_btn_drag_prev_pos_y_;

			glm::vec3 from_target = position_ - look_at_;
			from_target = glm::rotate(from_target, -y_offset * delta_time * mouse_sensitivty_, right_);
			from_target = glm::rotateY(from_target, -x_offset * delta_time * mouse_sensitivty_);

			position_ = look_at_ + from_target;
			UpdateTransform();

			right_mouse_btn_drag_prev_pos_x_ = x;
			right_mouse_btn_drag_prev_pos_y_ = y;
		}

		glm::vec3 movement = delta_time * movement_speed_ * movement_ * orientation_;
		position_ += movement;
		look_at_ += movement;
		transform_mat_ = GetViewMat();
	}

	void Camera::MouseButtonHandler(int button, int action) {
		if (button == GLFW_MOUSE_BUTTON_LEFT) {
			if (action == GLFW_PRESS) {
				left_mouse_btn_drag_engaged_ = true;
				double x, y;
				glfwGetCursorPos(Engine::window_, &x, &y);
				left_mouse_btn_drag_prev_pos_x_ = x;
				left_mouse_btn_drag_prev_pos_y_ = y;
			}
			else if (action == GLFW_RELEASE) {
				left_mouse_btn_drag_engaged_ = false;
			}
		}
		else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
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

	void Camera::KeyHandler(int key, int action) {
		switch (key)
		{
		case GLFW_KEY_W:
			if (action == GLFW_PRESS) {
				movement_.z = -1.0f;
			}
			else if (action == GLFW_RELEASE) {
				movement_.z = 0.0f;
			}
			break;
		case GLFW_KEY_S:
			if (action == GLFW_PRESS) {
				movement_.z = 1.0f;
			}
			else if (action == GLFW_RELEASE) {
				movement_.z = 0.0f;
			}
			break;
		case GLFW_KEY_A:
			if (action == GLFW_PRESS) {
				movement_.x = -1.0f;
			}
			else if (action == GLFW_RELEASE) {
				movement_.x = 0.0f;
			}
			break;
		case GLFW_KEY_D:
			if (action == GLFW_PRESS) {
				movement_.x = 1.0f;
			}
			else if (action == GLFW_RELEASE) {
				movement_.x = 0.0f;
			}
			break;
		case GLFW_KEY_Z:
			if (action == GLFW_PRESS) {
				movement_.y = -1.0f;
			}
			else if (action == GLFW_RELEASE) {
				movement_.y = 0.0f;
			}
			break;
		case GLFW_KEY_X:
			if (action == GLFW_PRESS) {
				movement_.y = 1.0f;
			}
			else if (action == GLFW_RELEASE) {
				movement_.y = 0.0f;
			}
			break;
		case GLFW_KEY_C:
			if (action == GLFW_PRESS) {
				Reset();
			}
			break;
		}
	}

	glm::mat4 Camera::GetProjectionMat() {
		return glm::perspective(fov_, aspect_, near_, far_);
	}

	glm::mat4 Camera::GetViewMat() {
		return glm::lookAt(position_, look_at_, glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::mat4 Camera::GetMVPMat(const glm::mat4& model_mat) {
		return GetProjectionMat() * model_mat * GetViewMat();
	}

	void Camera::UpdateTransform() {
		transform_mat_ = GetViewMat();
		orientation_ = glm::quat(transform_mat_);
		forward_ = glm::vec3(0.0f, 0.0f, 1.0f) * orientation_;
		right_ = glm::vec3(1.0f, 0.0f, 0.0f) * orientation_;
		up_ = glm::vec3(0.0f, 1.0f, 0.0f) * orientation_;
	}

}


