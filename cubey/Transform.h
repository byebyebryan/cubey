#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtx/rotate_vector.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/transform.hpp"

namespace cubey {
	class Transform
	{
	public:
		Transform() : 
			transformation_mat_(glm::mat4()),
			position_(glm::vec3()),
			orientation_(glm::quat()),
			scale_(glm::vec3(1.0f, 1.0f, 1.0f)),
			euler_angles_(glm::vec3()),
			forward_(glm::vec3(0.0f, 0.0f, 1.0f)),
			right_(glm::vec3(1.0f, 0.0f, 0.0f)),
			up_(glm::vec3(0.0f, 1.0f, 0.0f)) {
		}

		Transform(const glm::vec3& _position) : 
			transformation_mat_(glm::mat4()),
			position_(_position),
			orientation_(glm::quat()),
			scale_(glm::vec3(1.0f, 1.0f, 1.0f)),
			euler_angles_(glm::vec3()),
			forward_(glm::vec3(0.0f, 0.0f, 1.0f)),
			right_(glm::vec3(1.0f, 0.0f, 0.0f)),
			up_(glm::vec3(0.0f, 1.0f, 0.0f)) {
			RefreshMat();
		}

		Transform(const glm::vec3& _position, const glm::quat& _orientation) : 
			transformation_mat_(glm::mat4()),
			position_(_position),
			orientation_(glm::quat()),
			scale_(glm::vec3(1.0f, 1.0f, 1.0f)),
			euler_angles_(glm::vec3()),
			forward_(glm::vec3(0.0f, 0.0f, 1.0f)),
			right_(glm::vec3(1.0f, 0.0f, 0.0f)),
			up_(glm::vec3(0.0f, 1.0f, 0.0f)) {
			RotateTo(_orientation);
		}

		Transform(const glm::vec3& _position, const glm::quat& _orientation, const glm::vec3& _scale) :
			transformation_mat_(glm::mat4()),
			position_(_position),
			orientation_(glm::quat()),
			scale_(_scale),
			euler_angles_(glm::vec3()),
			forward_(glm::vec3(0.0f, 0.0f, 1.0f)),
			right_(glm::vec3(1.0f, 0.0f, 0.0f)),
			up_(glm::vec3(0.0f, 1.0f, 0.0f)) {
			RotateTo(_orientation);
		}

		void TranslateTo(const glm::vec3& _position) {
			position_ = _position;
			RefreshMat();
		}
		void Translate(const glm::vec3& _translation) {
			TranslateTo(position_ + _translation);
		}

		void RotateTo(const glm::quat& _rotation) {
			orientation_ = _rotation;
			euler_angles_ = glm::degrees(glm::eulerAngles(orientation_));
			forward_ = glm::vec3(0.0f, 0.0f, 1.0f) * orientation_;
			right_ = glm::vec3(1.0f, 0.0f, 0.0f) * orientation_;
			up_ = glm::vec3(0.0f, 1.0f, 0.0f) * orientation_;
			RefreshMat();
		}
		void Rotate(const glm::quat& _rotation) {
			RotateTo(orientation_ * _rotation);
		}

		void ScaleTo(const glm::vec3& _scale) {
			scale_ = _scale;
			RefreshMat();
		}
		void Scale(const glm::vec3& _scale) {
			ScaleTo(scale_ * scale_);
		}

		void LookAt(const glm::vec3& target) {
			LookAt(target, glm::vec3(0.0f, 1.0f, 0.0f));
		}

		void LookAt(const glm::vec3& target, const glm::vec3& _up) {
			RotateTo(glm::quat(glm::lookAt(target, position_, _up)));
		}

		glm::mat4 transformation_mat() { return transformation_mat_; }
		glm::vec3 position() { return position_; }
		glm::quat orientation() { return orientation_; }
		glm::vec3 scale() { return scale_; }
		glm::vec3 euler_angles() { return euler_angles_; }
		glm::vec3 forward() { return forward_; }
		glm::vec3 right() { return right_; }
		glm::vec3 up() { return up_; }
	protected:
		void RefreshMat() {
			transformation_mat_ = glm::scale(scale_) * glm::mat4_cast(orientation_) * glm::translate(position_);
		}

		glm::mat4 transformation_mat_;

		glm::vec3 position_;
		glm::quat orientation_;
		glm::vec3 scale_;

		glm::vec3 euler_angles_;

		glm::vec3 forward_;
		glm::vec3 right_;
		glm::vec3 up_;

		friend class UI;
	};
}


