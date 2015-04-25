#include "Fractal2DDemo.h"

#include "MainMenu.h"

#include "glm/gtx/transform2.hpp"

namespace cubey {

	void Fractal2DDemo::StartUp() {

		PushToEngine();

		sp_fractal_ = ShaderManager::Get()->CreateProgram("fractal_2d.__VS_FRACTAL__.__FS_FRACTAL__");
		mi_fsq_ = PrimitiveFactory::FullScreenQuad()->CreateInstance(sp_fractal_);

		type_ = 1;

		order_ = 2;

		c_real_ = 0.35f;
		c_imaginary_ = -0.35f;

		max_iteration_ = 512;

		starting_hue_ = 0.05f;

		zoom_ = 1.0f;
		center_ = { 0.0f, 0.0f };

		//impl_type_ = 1;

		MainMenu::Get()->AddReturnToMenuButton(this);

		TwUI::Get()->AddDefaultInfo();

		//TwUI::Get()->AddRW("cpu/gpu", TW_TYPE_INT16, &impl_type_, "min=1 max=1 step=1 group=Fractal");
		TwUI::Get()->AddRW("julia/mandelbrot", TW_TYPE_INT16, &type_, "min=0 max=1 step=1 group=Fractal");
		TwUI::Get()->AddRW("order", TW_TYPE_INT16, &order_, "min=2 max=8 step=1 group=Fractal");
		TwUI::Get()->AddRW("max iteration", TW_TYPE_INT16, &max_iteration_, "min=64 max=1024 step=64 group=Fractal");
		TwUI::Get()->AddRW("starting hue", TW_TYPE_FLOAT, &starting_hue_, "precision=2 min=0 max=1 step=0.05 group=Fractal");
		TwUI::Get()->AddRW("c real", TW_TYPE_DOUBLE, &c_real_, "precision=2 min=-1 max=1 step=0.05 group=Julia");
		TwUI::Get()->AddRW("c imaginary", TW_TYPE_DOUBLE, &c_imaginary_, "precision=2 min=-1 max=1 step=0.05 group=Julia");
	}

	void Fractal2DDemo::Update(float delta_time) {
		GLint viewport_size[4];
		glGetIntegerv(GL_VIEWPORT, viewport_size);

		viewport_aspect_ratio_ = (float)viewport_size[2] / (float)viewport_size[3];

		zoom_ *= 1.0f - Input::Get()->mouse_wheel_offset() * Time::delta_time() * 10.0f;
		if (Input::Get()->is_left_mouse_btn_down()) {
			center_ -= glm::dvec2(Input::Get()->mouse_pos_offset()) * Time::delta_time() * zoom_ / (viewport_size[3] / 100.0);
		}
	}

	void Fractal2DDemo::Render() {
		sp_fractal_->Activate();

		sp_fractal_->SetUniform("u_zoom", (float)zoom_);
		sp_fractal_->SetUniform("u_move", glm::vec2(center_));
		sp_fractal_->SetUniform("u_aspect_ratio", (float)viewport_aspect_ratio_);
		sp_fractal_->SetUniform("u_type", type_);
		sp_fractal_->SetUniform("u_max_iterations", max_iteration_);
		sp_fractal_->SetUniform("u_order", order_);
		sp_fractal_->SetUniform("u_starting_hue", starting_hue_);

		sp_fractal_->SetUniform("u_c", glm::vec2(c_real_, c_imaginary_));

		mi_fsq_->Draw();

	}

	void Fractal2DDemo::CloseDown() {
		PopFromEngine();

		ShaderManager::Get()->DestroyProgram(sp_fractal_);
		delete mi_fsq_;
	}

}