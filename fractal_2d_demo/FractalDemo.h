#pragma once

#include "cubey.h"

namespace cubey {
	class FractalDemo : public EngineEventsBase, public AutoXmlBase<FractalDemo> {
	public:
		void Init() override;
		//void Update(float delta_time) override;
		void Render() override;
		void Update(float delta_time) override;

	private:

		MeshInstance* mi_fsq_;

		ShaderProgram* sp_fractal_;

		int impl_type_;
		int type_;
		int max_iteration_;
		double c_real_;
		double c_imaginary_;
		int order_;
		float starting_hue_;

		double zoom_;
		glm::dvec2 center_;
		double viewport_aspect_ratio_;
	};

}


