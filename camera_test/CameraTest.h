#pragma once

#include "cubey.h"

namespace cubey {
	class CameraTest : public EngineEventsBase, public AutoXmlBase<CameraTest> {
	public:
		void Init() override;
		void Render() override;

	private:

		GLuint t2_fp_;
		GLuint fb_fp_;
		MeshInstance* mi_fsq_;
		MeshInstance* mi_box_;

		ShaderProgram* sp_first_;
		ShaderProgram* sp_second_;
	};

}


