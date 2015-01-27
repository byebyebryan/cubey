#include "Camera.h"

#include "GLFW/glfw3.h"
#include "Input.h"

namespace cubey {
	const float kDefaultFOV = 60.0f;
	const float kDefaultZNear = 1.0f;
	const float kDefaultZFar = 500.0f;
	const glm::vec3 kDefaultPosition = glm::vec3(0.0f, 0.0f, -15.0f);
	const glm::vec3 kDefaultLookAtTargetPos = glm::vec3();

	const float kDefaultMouseSensitivity = 1.0f;
	const float kDefaultMovementSpeed = 5.0f;

	const float kDefautlMouseWheelSpeed = 5.0f;

	const float kDefaultPitchLimit = 85.0f;

	Camera::Camera() {
		init_lisenter_ = EventLisenter<Engine::InitEvent>([this](const Engine::InitEvent& e){
			Init();
		});
		update_lisenter_ = EventLisenter<Engine::UpdateEvent>([this](const Engine::UpdateEvent& e){
			Update(e.deltatime);
		});
		init_lisenter_.PushToChannel();
		update_lisenter_.PushToChannel();
	}

	void Camera::Init() {
		Reset();
	}

	void Camera::Reset() {
		fov_ = glm::radians(kDefaultFOV);
		z_near_ = kDefaultZNear;
		z_far_ = kDefaultZFar;

		int w, h;
		glfwGetFramebufferSize(Engine::window_, &w, &h);
		aspect_ = w / (float)h;

		transform_.TranslateTo(kDefaultPosition);
		look_at_target_pos_ = kDefaultLookAtTargetPos;
		look_at_target_ = nullptr;
		transform_.LookAt(look_at_target_pos_);

		projection_mat_ = glm::perspective(fov_, aspect_, z_near_, z_far_);

		mouse_sensitivty_ = kDefaultMouseSensitivity;
		movement_speed_ = kDefaultMovementSpeed;

		mouse_wheel_speed_ = kDefautlMouseWheelSpeed;

		pitch_limit_ = kDefaultPitchLimit;
		//!!!!!!!!!!!!
		//these should be computed from initial orientation
		yaw_ = 0.0f;
		pitch_ = 0.0f;
		roll_ = 0.0f;

		flip_mat_ = glm::scale(glm::vec3(-1.0f, 1.0f, 1.0f));
		view_mat_ = glm::lookAt(transform_.position(), look_at_target_pos_, transform_.up()) * flip_mat_;
	}

	void Camera::Update(float delta_time) {
		if (look_at_target_) {
			look_at_target_pos_ = look_at_target_->position();
		}

		if (Input::Main()->is_left_mouse_btn_down()) {
			Orbit(-Input::Main()->mouse_pos_offset().x * delta_time * mouse_sensitivty_, Input::Main()->mouse_pos_offset().y * delta_time * mouse_sensitivty_);
		}
		if (Input::Main()->is_right_mouse_btn_down()) {
			PanTilt(Input::Main()->mouse_pos_offset().x * delta_time * mouse_sensitivty_, -Input::Main()->mouse_pos_offset().y * delta_time * mouse_sensitivty_);
		}

		float mouse_wheel_movement = delta_time * Input::Main()->mouse_wheel_offset() * mouse_wheel_speed_;
		transform_.Translate(transform_.forward() * mouse_wheel_movement);

		glm::vec3 movement = delta_time * movement_speed_ * Input::Main()->movement() * transform_.orientation();
		transform_.Translate(movement);
		look_at_target_pos_ += movement;

		view_mat_ = glm::lookAt(transform_.position(), look_at_target_pos_, transform_.up()) * flip_mat_;
	}

	void Camera::LookAt(glm::vec3 target_pos) {
		look_at_target_pos_ = target_pos;
		transform_.LookAt(look_at_target_pos_, transform_.up());
		view_mat_ = glm::lookAt(transform_.position(), look_at_target_pos_, transform_.up()) * flip_mat_;
	}

	void Camera::PanTilt(float yaw, float pitch) {
		glm::vec3 to_target = look_at_target_pos_ - transform_.position();

		yaw_ += glm::degrees(yaw);
		if (yaw_ > 180.f) {
			yaw_ -= 180.f;
		}
		else if (yaw_ < -180.f) {
			yaw_ += 180.f;
		}
		glm::quat q_yaw = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
		transform_.Rotate(q_yaw);
		to_target = to_target * q_yaw;

		pitch_ += glm::degrees(pitch);
		if (pitch_ > -pitch_limit_ && pitch_ < pitch_limit_) {
			glm::quat q_pitch = glm::angleAxis(pitch, transform_.right());
			transform_.Rotate(q_pitch);
			to_target = to_target * q_pitch;
		}
		else {
			pitch_ -= glm::degrees(pitch);
		}
		
		look_at_target_pos_ = transform_.position() + to_target;
	}

	void Camera::Orbit(float yaw, float pitch) {
		glm::vec3 from_target = transform_.position() - look_at_target_pos_;

		yaw_ += glm::degrees(yaw);
		if (yaw_ > 180.f) {
			yaw_ -= 180.f;
		}
		else if (yaw_ < -180.f) {
			yaw_ += 180.f;
		}
		glm::quat q_yaw = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
		from_target = from_target * q_yaw;
		transform_.Rotate(q_yaw);

		pitch_ += glm::degrees(pitch);
		if (pitch_ > -pitch_limit_ && pitch_ < pitch_limit_) {
			glm::quat q_pitch = glm::angleAxis(pitch, transform_.right());
			from_target = from_target * q_pitch;
			transform_.Rotate(q_pitch);
		}
		else {
			pitch_ -= glm::degrees(pitch);
		}

		transform_.TranslateTo(look_at_target_pos_ + from_target);
	}

	

}


