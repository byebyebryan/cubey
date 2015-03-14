#include "FractalDemo.h"

#include "ObjLoader.h"
#include "glm/gtx/transform2.hpp"

namespace cubey {

	void FractalDemo::Init() {

		sp_fractal_ = ShaderManager::Get()->CreateProgram("debug.__VS_FRACTAL__.__FS_FRACTAL__");
		mi_fsq_ = PrimitiveFactory::FullScreenQuad()->CreateInstance(sp_fractal_);

		type_ = 1;

		order_ = 2;

		c_real_ = 0.35f;
		c_imaginary_ = -0.35f;

		max_iteration_ = 512;

		starting_hue_ = 0.05f;

		zoom_ = 1.0f;
		center_ = { 0.0f, 0.0f };

		impl_type_ = 1;

		TwUI::Get()->AddRW("cpu/gpu", TW_TYPE_INT16, &impl_type_, "min=1 max=1 step=1 group=Fractal");
		TwUI::Get()->AddRW("julia/mandelbrot", TW_TYPE_INT16, &type_, "min=0 max=1 step=1 group=Fractal");
		TwUI::Get()->AddRW("order", TW_TYPE_INT16, &order_, "min=2 max=8 step=1 group=Fractal");
		TwUI::Get()->AddRW("max iteration", TW_TYPE_INT16, &max_iteration_, "min=64 max=1024 step=64 group=Fractal");
		TwUI::Get()->AddRW("starting hue", TW_TYPE_FLOAT, &starting_hue_, "precision=2 min=0 max=1 step=0.05 group=Fractal");
		TwUI::Get()->AddRW("c real", TW_TYPE_DOUBLE, &c_real_, "precision=2 min=-1 max=1 step=0.05 group=Julia");
		TwUI::Get()->AddRW("c imaginary", TW_TYPE_DOUBLE, &c_imaginary_, "precision=2 min=-1 max=1 step=0.05 group=Julia");
	}

	void FractalDemo::Update(float delta_time) {
		GLint viewport_size[4];
		glGetIntegerv(GL_VIEWPORT, viewport_size);

		viewport_aspect_ratio_ = (float)viewport_size[2] / (float)viewport_size[3];

		zoom_ *= 1.0f - Input::Get()->mouse_wheel_offset() * Time::delta_time() * 10.0f;
		if (Input::Get()->is_left_mouse_btn_down()) {
			center_ -= glm::dvec2(Input::Get()->mouse_pos_offset()) * Time::delta_time() * zoom_ / (viewport_size[3] / 100.0);
		}
	}

	glm::dvec2 CMultiply(const glm::dvec2& a, const glm::dvec2& b) {
		return glm::dvec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
	}

	void FractalDemo::Render() {
		if (impl_type_ == 0) {
			/*GLint viewport_size[4];
			glGetIntegerv(GL_VIEWPORT, viewport_size);
			int w = viewport_size[2];
			int h = viewport_size[3];

			std::vector<float> pixels;
			pixels.reserve(w * h);

			for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
			glm::dvec2 p = (glm::dvec2((double)x / ((double)w * viewport_aspect_ratio_), (double)y / (double)h) * 2.0 - 1.0) * zoom_ + center_ * glm::dvec2(1.0, -1.0);

			glm::dvec2 z, c, dz;

			if (type_ == 0) {
			c = glm::dvec2(c_real_, c_imaginary_);
			z = p;
			dz = glm::dvec2(1.0, 0.0);
			}
			else {
			c = p;
			z = glm::dvec2(0.0, 0.0);
			dz = glm::dvec2(0.0, 0.0);
			}

			int i;
			for (i = 0; i < 16; i++) {
			dz = 2.0 * CMultiply(z, dz);
			if (type_ == 1) {
			dz += glm::dvec2(1.0, 0.0);
			}

			z = CMultiply(z, z) + c;

			if (glm::dot(z, z) > 1024) break;
			}

			double d = glm::sqrt(glm::dot(z, z) / glm::dot(dz, dz)) * glm::log(glm::dot(z, z));
			double c1 = glm::clamp(4.0*d / zoom_, 0.0, 1.0);
			c1 = glm::pow(c1, 0.25);

			pixels.push_back((float)c1);
			}
			}*/
		}
		else {
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
		
	}

	

}