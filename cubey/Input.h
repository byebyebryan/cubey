#pragma once

#include "glm/glm.hpp"
#include "Interfaces.h"

namespace cubey {
	class Input : public IEngineEvents, public ISingleton<Input> {
	public:
		Input();

		void SystemInit() override;
		
		bool is_left_mouse_btn_down() { return is_left_mouse_btn_down_; }
		bool is_right_mouse_btn_down() { return is_right_mouse_btn_down_; }
		glm::vec2 mouse_pos() { return mouse_pos_; }
		glm::vec2 mouse_pos_offset() { return mouse_pos_offset_; }
		float mouse_wheel_offset() { return mouse_wheel_offset_; }
		glm::vec3 movement() { return movement_; }


		bool mouse_button_consumed_by_ui_;
		bool mouse_wheel_consumed_by_ui_;
	private:
		void StartUp() override;
		void EarlyUpdate(float delta_time) override;

		void MouseButtonHandler(int button, int action, int mods);
		void MousePosHandler(float xpos, float ypos);
		void MouseWheelHandler(float yoffset);
		void KeyHandler(int key, int action, int mods);

		bool is_left_mouse_btn_down_;
		bool is_right_mouse_btn_down_;

		glm::vec2 mouse_pos_;
		glm::vec2 mouse_pos_offset_;
		float mouse_wheel_offset_;
		glm::vec3 movement_;

		glm::vec2 mouse_pos_prev_;
		float mouse_wheel_offset_prev_;
	};
}

