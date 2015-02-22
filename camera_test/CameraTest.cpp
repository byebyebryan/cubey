#include "CameraTest.h"

namespace cubey {

	void CameraTest::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_TEXTURE_2D);
		glClearColor(0.4, 0.4, 0.4, 1);

		glGenTextures(1, &t2_fp_);
		glBindTexture(GL_TEXTURE_2D, t2_fp_);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 320, 180, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

		glGenFramebuffers(1, &fb_fp_);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_fp_);
		glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t2_fp_, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

		sp_first_ = ShaderManager::Get()->CreateProgram("debug.__VS_FIRST_PASS__.__FS_FIRST_PASS__");
		sp_second_ = ShaderManager::Get()->CreateProgram("debug.__VS_SECOND_PASS__.__FS_SECOND_PASS__");

		mi_box_ = PrimitiveFactory::UnitBallWNormal(glm::vec3(1.0))->CreateInstance(sp_first_, "u_mvp_mat", "u_normal_mat");
		mi_fsq_ = PrimitiveFactory::FullScreenQuad()->CreateInstance(sp_second_);
	}

	void CameraTest::Render() {
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_fp_);
		glViewport(0, 0, 320, 180);
		glClear(GL_COLOR_BUFFER_BIT);
		sp_first_->Activate();
		sp_first_->SetUniform("u_view_mat", MainCamera::Get()->view_mat());
		
		mi_box_->Draw();

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glViewport(0, 0, 1280, 720);
		glClear(GL_COLOR_BUFFER_BIT);
		sp_second_->Activate();
		mi_fsq_->Draw();
	}

}