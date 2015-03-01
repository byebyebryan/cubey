#pragma once

#include "cubey.h"

namespace cubey {
	class CameraTest : public EngineEventsBase, public AutoXmlBase<CameraTest> {
	public:
		void Init() override;
		void Render() override;

	private:

		GLuint t2_shadow_map_;
		GLuint fb_shadow_pass_;

		GLuint tc_shadow_map_;

		GLuint t2_fp_c_;
		GLuint t2_fp_d_;
		GLuint fb_fp_;
		MeshInstance* mi_fsq_;
		MeshInstance* mi_plane_;
		MeshInstance* mi_box_;
		MeshInstance* mi_ball_;
		MeshInstance* mi_ball_2_;
		MeshInstance* mi_tea_pot_;

		ShaderProgram* sp_shadow_pass_;
		ShaderProgram* sp_first_;
		ShaderProgram* sp_second_;
	};

}


