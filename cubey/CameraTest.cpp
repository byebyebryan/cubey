#include "CameraTest.h"

namespace cubey {

	void CameraTest::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glClearColor(0.4, 0.4, 0.4, 1);

		prog = ShaderManager::Main()->CreateProgram("debug.VS_LIT.FS_LIT");

		mesh = Mesh::Create(PrimitiveFactory::AxisIndicator(), GL_TRIANGLES);
		mesh_instance = mesh->CreateInstance(prog, "u_mvp_mat", "u_normal_mat");
	}

	void CameraTest::Render() {

		prog->Activate();
		prog->SetUniform("u_view_mat", Camera::Main()->view_mat());

		mesh_instance->Draw();
	}

}