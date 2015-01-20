#include "Camera.h"

#include "GLFW/glfw3.h"
#include "glm/gtx/rotate_vector.hpp"
#include "Engine.h"
#include "Input.h"

namespace cubey {
	const float kDefaultFOV = 60.0f;
	const float kDefaultNear = 1.0f;
	const float kDefaultFar = 500.0f;
	const glm::vec3 kDefaultPosition = glm::vec3(0.0f, 0.0f, 10.0f);
	const glm::vec3 kDefaultLookAt = glm::vec3();

	const float kDefaultMouseSensitivity = 1.0f;
	const float kDefaultMovementSpeed = 5.0f;

	Camera::Camera() {
		update_listener_ = EventLisenter<Engine::UpdateEvent>([this](const Engine::UpdateEvent& e){
			Update(e.deltatime);
		});
	}

	void Camera::Init() {
		update_listener_.PushToChannel();
		Reset();
	}

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

		mouse_sensitivty_ = kDefaultMouseSensitivity;
		movement_speed_ = kDefaultMovementSpeed;
	}

	void Camera::Update(float delta_time) {
		
		if (Input::Main()->is_left_mouse_btn_down()) {
			glm::vec3 to_target = look_at_ - position_;
			to_target = glm::rotate(to_target, -Input::Main()->mouse_pos_offset().y * delta_time * mouse_sensitivty_, right_);
			to_target = glm::rotateY(to_target, -Input::Main()->mouse_pos_offset().x * delta_time * mouse_sensitivty_);

			look_at_ = position_ + to_target;
			UpdateTransform();
		}
		if (Input::Main()->is_right_mouse_btn_down()) {

			glm::vec3 from_target = position_ - look_at_;
			from_target = glm::rotate(from_target, -Input::Main()->mouse_pos_offset().y * delta_time * mouse_sensitivty_, right_);
			from_target = glm::rotateY(from_target, -Input::Main()->mouse_pos_offset().x * delta_time * mouse_sensitivty_);

			position_ = look_at_ + from_target;
			UpdateTransform();
		}

		glm::vec3 movement = delta_time * movement_speed_ * Input::Main()->movement() * orientation_;
		position_ += movement;
		look_at_ += movement;
		transform_mat_ = GetViewMat();
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


