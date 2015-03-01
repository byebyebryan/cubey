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
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_shadow_pass_);
		glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tc_shadow_map_, 0);

		glGenTextures(1, &tc_reflection_map_c_);
		glBindTexture(GL_TEXTURE_CUBE_MAP, tc_reflection_map_c_);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

		for (int i = 0; i < 6; i++) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, 1024, 1024, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		}

		glGenTextures(1, &tc_reflection_map_d_);
		glBindTexture(GL_TEXTURE_CUBE_MAP, tc_reflection_map_d_);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

		for (int i = 0; i < 6; i++) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT, 1024, 1024, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		}

		glGenFramebuffers(1, &fb_reflection_pass_);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_reflection_pass_);
		glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tc_reflection_map_c_, 0);
		glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tc_reflection_map_d_, 0);

		sp_shadow_pass_ = ShaderManager::Get()->CreateProgram("debug.__VS_CUBE_SHADOW_PASS__.__GS_CUBE_SHADOW_PASS__.__FS_CUBE_SHADOW_PASS__");
		sp_reflection_pass_ = ShaderManager::Get()->CreateProgram("debug.__VS_CUBE_REFLECTION_PASS__.__GS_CUBE_REFLECTION_PASS__.__FS_CUBE_REFLECTION_PASS__");
		sp_render_pass_ = ShaderManager::Get()->CreateProgram("debug.__VS_RENDER_PASS__.__FS_RENDER_PASS__");
		//sp_second_ = ShaderManager::Get()->CreateProgram("debug.__VS_SECOND_PASS__.__FS_SECOND_PASS__");

		mi_box_ = PrimitiveFactory::UnitSphere(6)->CreateInstance(sp_render_pass_, "u_mvp_mat");

		mi_ball_ = PrimitiveFactory::UnitSphere(6)->CreateInstance(sp_render_pass_, "u_mvp_mat");
		mi_ball_->transform_.Translate(glm::vec3(2.0f, 0.0f, 0.0f));

		mi_ball_2_ = PrimitiveFactory::UnitSphere(6)->CreateInstance(sp_render_pass_, "u_mvp_mat");
		mi_ball_2_->transform_.Translate(glm::vec3(0.0f, 0.0f, 2.0f));

		mi_plane_ = PrimitiveFactory::UnitXZPlane()->CreateInstance(sp_render_pass_, "u_mvp_mat");
		mi_plane_->transform_.Translate(glm::vec3(0.0f, -1.0f, 0.0f));
		mi_plane_->transform_.Scale(glm::vec3(10.0f, 1.0f, 10.0f));

		//mi_fsq_ = PrimitiveFactory::FullScreenQuad()->CreateInstance(sp_render_pass_, "u_mvp_mat");
		//mi_tea_pot_ = ObjLoader::LoadObj("teapot.obj")->CreateInstance(sp_first_, "u_mvp_mat", "u_normal_mat");
	}

	std::vector<glm::mat4> CubeMVPMatrices(const glm::vec3& position, const glm::mat4& model_mat) {
		glm::mat4 projection_mat = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
		std::vector<glm::mat4> mvp_mats(6);

		glm::vec3 targets[6] = { { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } };
		glm::vec3 ups[6] = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };

		for (int i = 0; i < 6; i++) {
			mvp_mats[i] = projection_mat * glm::lookAt(position, position + targets[i], ups[i]) * glm::scale(glm::vec3(1.0f, 1.0f, 1.0f)) * model_mat;
		}

		return mvp_mats;
	}

	void CameraTest::Render() {
		/*glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_fp_);
		glViewport(0, 0, 320, 180);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		GLuint e = glGetError();*/

		

		//glEnable(GL_POLYGON_OFFSET_FILL);
		//glPolygonOffset(2.0f, 4.0f);

		
		glm::vec3 light_position = { -1.0f, 0.0f, -1.0f };

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_shadow_pass_);

		glCullFace(GL_FRONT);
		glViewport(0, 0, 1024, 1024);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		sp_shadow_pass_->Activate();

		sp_shadow_pass_->SetUniform("u_mvp_mats[0]", CubeMVPMatrices(light_position, mi_plane_->transform_.transformation_mat()));
		mi_plane_->mesh_->Draw();
		
		sp_shadow_pass_->SetUniform("u_mvp_mats[0]", CubeMVPMatrices(light_position, mi_box_->transform_.transformation_mat()));
		mi_box_->mesh_->Draw();

		sp_shadow_pass_->SetUniform("u_mvp_mats[0]", CubeMVPMatrices(light_position, mi_ball_->transform_.transformation_mat()));
		mi_ball_->mesh_->Draw();

		sp_shadow_pass_->SetUniform("u_mvp_mats[0]", CubeMVPMatrices(light_position, mi_ball_2_->transform_.transformation_mat()));
		mi_ball_2_->mesh_->Draw();

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_reflection_pass_);

		glViewport(0, 0, 1024, 1024);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glCullFace(GL_BACK);

		glBindTexture(GL_TEXTURE_CUBE_MAP, tc_shadow_map_);

		sp_reflection_pass_->Activate();

		sp_reflection_pass_->SetUniform("u_light.position", light_position);
		sp_reflection_pass_->SetUniform("u_light.ambient", glm::vec3(0.1f));
		sp_reflection_pass_->SetUniform("u_light.color", glm::vec3(1.0f));
		sp_reflection_pass_->SetUniform("u_light.attenuation", glm::vec3(1.0f, 0.5f, 0.25f));
		sp_reflection_pass_->SetUniform("u_light.t_shadow_map", 0);

		sp_reflection_pass_->SetUniform("u_material.specular", glm::vec3(1.0f));
		sp_reflection_pass_->SetUniform("u_material.emission", glm::vec3(0.0f));
		sp_reflection_pass_->SetUniform("u_material.alpha", 1.0f);

		sp_reflection_pass_->SetUniform("u_eye_position", mi_box_->transform_.position());

		sp_reflection_pass_->SetUniform("u_mvp_mats[0]", CubeMVPMatrices(mi_box_->transform_.position(), mi_plane_->transform_.transformation_mat()));
		sp_reflection_pass_->SetUniform("u_model_mat", mi_plane_->transform_.transformation_mat());
		sp_reflection_pass_->SetUniform("u_normal_mat", glm::inverseTranspose(glm::mat3(mi_plane_->transform_.transformation_mat())));
		sp_reflection_pass_->SetUniform("u_material.diffuse", glm::vec3(1.0f, 1.0f, 1.0f));
		sp_reflection_pass_->SetUniform("u_material.shininess", 10.0f);
		mi_plane_->mesh_->Draw();

		/*sp_reflection_pass_->SetUniform("u_mvp_mats[0]", CubeMVPMatrices(mi_box_->transform_.position(), mi_box_->transform_.transformation_mat()));
		sp_reflection_pass_->SetUniform("u_model_mat", mi_box_->transform_.transformation_mat());
		sp_reflection_pass_->SetUniform("u_normal_mat", glm::inverseTranspose(glm::mat3(mi_box_->transform_.transformation_mat())));
		sp_reflection_pass_->SetUniform("u_material.diffuse", glm::vec3(1.0f, 0.0f, 0.0f));
		sp_reflection_pass_->SetUniform("u_material.shininess", 50.0f);
		mi_box_->mesh_->Draw();*/

		sp_reflection_pass_->SetUniform("u_mvp_mats[0]", CubeMVPMatrices(mi_box_->transform_.position(), mi_ball_->transform_.transformation_mat()));
		sp_reflection_pass_->SetUniform("u_model_mat", mi_ball_->transform_.transformation_mat());
		sp_reflection_pass_->SetUniform("u_normal_mat", glm::inverseTranspose(glm::mat3(mi_ball_->transform_.transformation_mat())));
		sp_reflection_pass_->SetUniform("u_material.diffuse", glm::vec3(0.0f, 1.0f, 0.0f));
		sp_reflection_pass_->SetUniform("u_material.shininess", 100.0f);
		mi_ball_->Draw();

		sp_reflection_pass_->SetUniform("u_mvp_mats[0]", CubeMVPMatrices(mi_box_->transform_.position(), mi_ball_2_->transform_.transformation_mat()));
		sp_reflection_pass_->SetUniform("u_model_mat", mi_ball_2_->transform_.transformation_mat());
		sp_reflection_pass_->SetUniform("u_normal_mat", glm::inverseTranspose(glm::mat3(mi_ball_2_->transform_.transformation_mat())));
		sp_reflection_pass_->SetUniform("u_material.diffuse", glm::vec3(0.0f, 0.0f, 1.0f));
		sp_reflection_pass_->SetUniform("u_material.shininess", 100.0f);
		mi_ball_2_->mesh_->Draw();

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glViewport(0, 0, 1280, 720);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glBindTexture(GL_TEXTURE_CUBE_MAP, tc_shadow_map_);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_CUBE_MAP, tc_reflection_map_c_);

		sp_render_pass_->Activate();

		sp_render_pass_->SetUniform("u_light.position", light_position);
		sp_render_pass_->SetUniform("u_light.ambient", glm::vec3(0.1f));
		sp_render_pass_->SetUniform("u_light.color", glm::vec3(1.0f));
		sp_render_pass_->SetUniform("u_light.attenuation", glm::vec3(1.0f, 0.5f, 0.25f));
		sp_render_pass_->SetUniform("u_light.t_shadow_map", 0);

		sp_render_pass_->SetUniform("u_material.specular", glm::vec3(1.0f));
		sp_render_pass_->SetUniform("u_material.emission", glm::vec3(0.0f));
		sp_render_pass_->SetUniform("u_material.alpha", 1.0f);

		sp_render_pass_->SetUniform("u_eye_position", MainCamera::Get()->transform_.position());

		sp_render_pass_->SetUniform("t_reflection_map", 1);

		sp_render_pass_->SetUniform("u_reflection_enabled", 0);
		sp_render_pass_->SetUniform("u_model_mat", mi_plane_->transform_.transformation_mat());
		sp_render_pass_->SetUniform("u_normal_mat", glm::inverseTranspose(glm::mat3(mi_plane_->transform_.transformation_mat())));
		sp_render_pass_->SetUniform("u_material.diffuse", glm::vec3(1.0f, 1.0f, 1.0f));
		sp_render_pass_->SetUniform("u_material.shininess", 10.0f);
		mi_plane_->Draw();

		sp_render_pass_->SetUniform("u_reflection_enabled", 1);
		sp_render_pass_->SetUniform("u_model_mat", mi_box_->transform_.transformation_mat());
		sp_render_pass_->SetUniform("u_normal_mat", glm::inverseTranspose(glm::mat3(mi_box_->transform_.transformation_mat())));
		sp_render_pass_->SetUniform("u_material.diffuse", glm::vec3(1.0f, 0.0f, 0.0f));
		sp_render_pass_->SetUniform("u_material.shininess", 100.0f);
		mi_box_->Draw();

		sp_render_pass_->SetUniform("u_reflection_enabled", 0);
		sp_render_pass_->SetUniform("u_model_mat", mi_ball_->transform_.transformation_mat());
		sp_render_pass_->SetUniform("u_normal_mat", glm::inverseTranspose(glm::mat3(mi_ball_->transform_.transformation_mat())));
		sp_render_pass_->SetUniform("u_material.diffuse", glm::vec3(0.0f, 1.0f, 0.0f));
		sp_render_pass_->SetUniform("u_material.shininess", 100.0f);
		mi_ball_->Draw();

		sp_render_pass_->SetUniform("u_reflection_enabled", 0);
		sp_render_pass_->SetUniform("u_model_mat", mi_ball_2_->transform_.transformation_mat());
		sp_render_pass_->SetUniform("u_normal_mat", glm::inverseTranspose(glm::mat3(mi_ball_2_->transform_.transformation_mat())));
		sp_render_pass_->SetUniform("u_material.diffuse", glm::vec3(0.0f, 0.0f, 1.0f));
		sp_render_pass_->SetUniform("u_material.shininess", 100.0f);
		mi_ball_2_->Draw();

		//sp_first_->SetUniform("u_mv_mat", MainCamera::Get()->view_mat() * mi_tea_pot_->transform_.transformation_mat());
		//mi_tea_pot_->Draw();

	}

}