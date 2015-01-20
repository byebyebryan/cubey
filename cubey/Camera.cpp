#include "Camera.h"

#include "GLFW/glfw3.h"

#include "Engine.h"
#include "Input.h"

namespace cubey {
	const float kDefaultFOV = 60.0f;
	const float kDefaultZNear = 1.0f;
	const float kDefaultZFar = 500.0f;
	const glm::vec3 kDefaultPosition = glm::vec3(0.0f, 0.0f, -5.0f);
	const glm::vec3 kDefaultLookAtTargetPos = glm::vec3();

	const float kDefaultMouseSensitivity = 1.0f;
	const float kDefaultMovementSpeed = 5.0f;

	const float kCameraPitchLimit = 85.0f;
	const float kCameraPitchLimitY = sin(glm::radians(kCameraPitchLimit));

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
		z_near_ = kDefaultZNear;
		z_far_ = kDefaultZFar;

		int w, h;
		glfwGetFramebufferSize(Engine::window_, &w, &h);
		aspect_ = w / (float)h;

		position_ = kDefaultPosition;
		look_at_target_pos_ = kDefaultLookAtTargetPos;
		look_at_target_ = nullptr;
		LookAt(look_at_target_pos_);

		projection_mat_ = glm::perspective(fov_, aspect_, z_near_, z_far_);

		mouse_sensitivty_ = kDefaultMouseSensitivity;
		movement_speed_ = kDefaultMovementSpeed;

		flip_mat_ = glm::scale(glm::vec3(-1.0f, 1.0f, 1.0f));
	}

	void Camera::Update(float delta_time) {
		if (look_at_target_) {
			look_at_target_pos_ = look_at_target_->position();
		}

		if (Input::Main()->is_left_mouse_btn_down()) {
			glm::vec3 from_target = position_ - look_at_target_pos_;
			glm::vec3 t_from_target = glm::rotate(from_target, -Input::Main()->mouse_pos_offset().y * delta_time * mouse_sensitivty_, right_);
			glm::vec3 n_from_target = glm::normalize(t_from_target);
			if (n_from_target.y <= kCameraPitchLimitY && n_from_target.y > -kCameraPitchLimitY) {
				from_target = t_from_target;
			}
			from_target = glm::rotateY(from_target, -Input::Main()->mouse_pos_offset().x * delta_time * mouse_sensitivty_);
			position_ = look_at_target_pos_ + from_target;
			LookAt(look_at_target_pos_);
		}
		if (Input::Main()->is_right_mouse_btn_down()) {
			glm::vec3 to_target = look_at_target_pos_ - position_;
			glm::vec3 t_to_target = glm::rotate(t_to_target, -Input::Main()->mouse_pos_offset().y * delta_time * mouse_sensitivty_, right_);
			glm::vec3 n_to_target = glm::normalize(t_to_target);
			if (n_to_target.y <= kCameraPitchLimitY && n_to_target.y > -kCameraPitchLimitY) {
				to_target = t_to_target;
			}
			to_target = glm::rotateY(to_target, -Input::Main()->mouse_pos_offset().x * delta_time * mouse_sensitivty_);
			look_at_target_pos_ = position_ + to_target;
			LookAt(look_at_target_pos_);
		}

		glm::vec3 movement = delta_time * movement_speed_ * Input::Main()->movement() * orientation_;
		Translate(movement);
		look_at_target_pos_ += movement;

		view_mat_ =  glm::lookAt(position_, look_at_target_pos_, up_) * flip_mat_;
	}

	glm::mat4 Camera::ProjectionMat() {
		return projection_mat_;
	}

	glm::mat4 Camera::ViewMat() {
		return view_mat_;
	}

	glm::mat4 Camera::MVPMat(const glm::mat4& model_mat) {
		return projection_mat_ * view_mat_ * model_mat;
	}

	glm::mat3 Camera::NormalMat(const glm::mat4& model_mat) {
		return glm::inverseTranspose(glm::mat3( view_mat_ * model_mat));
	}

}


