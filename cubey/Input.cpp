#include "Input.h"

#include "GLFW/glfw3.h"

namespace cubey {
	Input::Input() {
		is_left_mouse_btn_down_ = false;
		is_right_mouse_btn_down_ = false;

		mouse_pos_offset_ = glm::vec2();
		mouse_wheel_offset_ = mouse_wheel_offset_prev_ = 0.0f;
		movement_ = glm::vec3();

		mouse_button_consumed_by_ui_ = false;
		mouse_wheel_consumed_by_ui_ = false;
	}

	void Input::SystemInit() {
		EventChannel<Engine::MouseButtonEvent>::DirtyAdd([this](const Engine::MouseButtonEvent& e){
			MouseButtonHandler(e.button, e.action, e.mods);
		});
		EventChannel<Engine::MousePosEvent>::DirtyAdd([this](const Engine::MousePosEvent& e){
			MousePosHandler(e.xpos, e.ypos);
		});
		EventChannel<Engine::MouseWheelEvent>::DirtyAdd([this](const Engine::MouseWheelEvent& e){
			MouseWheelHandler(e.yoffset);
		});
		EventChannel<Engine::KeyEvent>::DirtyAdd([this](const Engine::KeyEvent& e){
			KeyHandler(e.key, e.action, e.mods);
		});

	}

	void Input::StartUp() {
		double x, y;
		glfwGetCursorPos(Engine::window_, &x, &y);
		mouse_pos_prev_ = mouse_pos_ = glm::vec2((float)x, (float)y);
	}

	void Input::EarlyUpdate(float delta_time) {
		mouse_pos_offset_ = mouse_pos_ - mouse_pos_prev_;
		mouse_pos_prev_ = mouse_pos_;
		mouse_wheel_offset_ = mouse_wheel_offset_prev_;
		mouse_wheel_offset_prev_ = 0.0f;
	}

	void Input::MouseButtonHandler(int button, int action, int mods) {
		if (mouse_button_consumed_by_ui_) {
			return;
		}

		if (button == GLFW_MOUSE_BUTTON_LEFT) {
			if (action == GLFW_PRESS) {
				is_left_mouse_btn_down_ = true;
			}
			else if (action == GLFW_RELEASE) {
				is_left_mouse_btn_down_ = false;
			}
		}
		else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
			if (action == GLFW_PRESS) {
				is_right_mouse_btn_down_ = true;
			}
			else if (action == GLFW_RELEASE) {
				is_right_mouse_btn_down_ = false;
			}
		}
	}

	void Input::MousePosHandler(float xpos, float ypos) {
		mouse_pos_.x = xpos;
		mouse_pos_.y = ypos;
	}

	void Input::MouseWheelHandler(float yoffset) {
		if (mouse_wheel_consumed_by_ui_) {
			return;
		}
		mouse_wheel_offset_prev_ += yoffset;
	}

	void Input::KeyHandler(int key, int action, int mods) {
		switch (key)
		{
		case GLFW_KEY_S:
			if (action == GLFW_PRESS) {
				movement_.z = -1.0f;
			}
			else if (action == GLFW_RELEASE) {
				movement_.z = 0.0f;
			}
			break;
		case GLFW_KEY_W:
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
		}
	}





}