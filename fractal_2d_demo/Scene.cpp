#include "CameraTest.h"

#include "ObjLoader.h"
#include "glm/gtx/transform2.hpp"

namespace cubey {

	void CameraTest::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_TEXTURE_CUBE_MAP);
		//glClearColor(0.4, 0.4, 0.4, 1);

		glGenTextures(1, &t2_shadow_map_);
		glBindTexture(GL_TEXTURE_2D, t2_shadow_map_);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, 1024, 1024, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

		glGenTextures(1, &tc_shadow_map_);
		glBindTexture(GL_TEXTURE_CUBE_MAP, tc_shadow_map_);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

		for (int i = 0; i < 6; i++) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, 0, GL_DEPTH_COMPONENT32F, 1024, 1024, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		}
		
		glGenFramebuffers(1, &fb_shadow_pass_);

		glGenTextures(1, &t2_fp_c_);
		glBindTexture(GL_TEXTURE_2D, t2_fp_c_);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 320, 180, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

		glGenTextures(1, &t2_fp_d_);
		glBindTexture(GL_TEXTURE_2D, t2_fp_d_);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 320, 180, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

		glGenFramebuffers(1, &fb_fp_);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_fp_);
		glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t2_fp_c_, 0);
		glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, t2_fp_d_, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

		sp_shadow_pass_ = ShaderManager::Get()->CreateProgram("debug.__VS_SHADOW_PASS__.__FS_SHADOW_PASS__");

		sp_first_ = ShaderManager::Get()->CreateProgram("debug.__VS_FIRST_PASS__.__FS_FIRST_PASS__");
		sp_second_ = ShaderManager::Get()->CreateProgram("debug.__VS_SECOND_PASS__.__FS_SECOND_PASS__");

		mi_box_ = PrimitiveFactory::UnitCube()->CreateInstance(sp_first_, "u_mvp_mat", "u_normal_mat");
		//mi_box_->transform_.Translate(glm::vec3(-2.0f,0.0f,0.0f));
		//mi_box_->transform_.Rotate(glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
		//mi_box_->transform_.Rotate(glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f)));

		mi_ball_ = PrimitiveFactory::UnitSphere(8)->CreateInstance(sp_first_, "u_mvp_mat", "u_normal_mat");
		mi_ball_->transform_.Translate(glm::vec3(2.0f, 0.0f, 0.0f));

		mi_ball_2_ = PrimitiveFactory::UnitSphere(8)->CreateInstance(sp_first_, "u_mvp_mat", "u_normal_mat");
		mi_ball_2_->transform_.Translate(glm::vec3(0.0f, 0.0f, 2.0f));

		mi_plane_ = PrimitiveFactory::UnitXZPlane()->CreateInstance(sp_first_, "u_mvp_mat", "u_normal_mat");
		mi_plane_->transform_.Translate(glm::vec3(0.0f, -1.0f, 0.0f));
		mi_plane_->transform_.Scale(glm::vec3(10.0f, 1.0f, 10.0f));

		mi_fsq_ = PrimitiveFactory::FullScreenQuad()->CreateInstance(sp_second_);

		//mi_tea_pot_ = ObjLoader::LoadObj("teapot.obj")->CreateInstance(sp_first_, "u_mvp_mat", "u_normal_mat");
	}

	void CameraTest::Render() {
		/*glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_fp_);
		glViewport(0, 0, 320, 180);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		GLuint e = glGetError();*/

		glViewport(0, 0, 1024, 1024);

		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(2.0f, 4.0f);

		glCullFace(GL_FRONT);

		glm::vec4 light_position = glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
		glm::mat4 light_projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

		glm::vec3 targets[6] = { { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} };
		glm::vec3 ups[6] = { { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
		
		sp_shadow_pass_->Activate();

		for (int i = 0; i < 6; i++) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_shadow_pass_);
			glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, tc_shadow_map_, 0);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glm::mat4 light_view = glm::lookAt(glm::vec3(light_position), glm::vec3(light_position) + targets[i], ups[i]);

			sp_shadow_pass_->SetUniform("u_mvp_mat", light_projection * light_view * mi_plane_->transform_.transformation_mat());
			mi_plane_->mesh_->Draw();

			sp_shadow_pass_->SetUniform("u_mvp_mat", light_projection * light_view * mi_box_->transform_.transformation_mat());
			mi_box_->mesh_->Draw();

			sp_shadow_pass_->SetUniform("u_mvp_mat", light_projection * light_view * mi_ball_->transform_.transformation_mat());
			mi_ball_->mesh_->Draw();

			sp_shadow_pass_->SetUniform("u_mvp_mat", light_projection * light_view * mi_ball_2_->transform_.transformation_mat());
			mi_ball_2_->mesh_->Draw();
		}

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glViewport(0, 0, 1280, 720);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glDisable(GL_POLYGON_OFFSET_FILL);
		glCullFace(GL_BACK);

		glBindTexture(GL_TEXTURE_CUBE_MAP, tc_shadow_map_);

		sp_first_->Activate();

		sp_first_->SetUniform("u_light_position", MainCamera::Get()->view_mat() * light_position);
		//sp_first_->SetUniform("u_light_position_ws", light_position);

		//glm::mat4 scale_bias_mat = glm::scaleBias<float, glm::highp>(0.5f, 0.5f);
		sp_first_->SetUniform("u_iv_mat", glm::inverse(MainCamera::Get()->view_mat()));
		
		//sp_first_->SetUniform("u_m_mat", mi_plane_->transform_.transformation_mat());
		sp_first_->SetUniform("u_mv_mat", MainCamera::Get()->view_mat() * mi_plane_->transform_.transformation_mat());
		mi_plane_->Draw();

		//sp_first_->SetUniform("u_m_mat", mi_box_->transform_.transformation_mat());
		sp_first_->SetUniform("u_mv_mat", MainCamera::Get()->view_mat() * mi_box_->transform_.transformation_mat());
		mi_box_->Draw();

		//sp_first_->SetUniform("u_m_mat", mi_ball_->transform_.transformation_mat());
		sp_first_->SetUniform("u_mv_mat", MainCamera::Get()->view_mat() * mi_ball_->transform_.transformation_mat());
		mi_ball_->Draw();

		//sp_first_->SetUniform("u_m_mat", mi_ball_2_->transform_.transformation_mat());
		sp_first_->SetUniform("u_mv_mat", MainCamera::Get()->view_mat() * mi_ball_2_->transform_.transformation_mat());
		mi_ball_2_->Draw();

		//sp_first_->SetUniform("u_mv_mat", MainCamera::Get()->view_mat() * mi_tea_pot_->transform_.transformation_mat());
		//mi_tea_pot_->Draw();

		/*glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, t2_fp_c_);
		glViewport(0, 0, 1280, 720);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		sp_second_->Activate();
		mi_fsq_->Draw();*/
	}

}