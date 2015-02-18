#include "CameraTest.h"

namespace cubey {

	void CameraTest::Init() {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glClearColor(0.4, 0.4, 0.4, 1);

		prog = ShaderManager::Get()->CreateProgram("debug.VS_LIT.FS_LIT");

		mesh_instance = PrimitiveFactory::UnitBoxWNormal(glm::vec3(1.0))->CreateInstance(prog, "u_mvp_mat", "u_normal_mat");

		TwBarInit();

		std::string buffer;
		WriteToBuffer(buffer);
		std::cout << buffer << std::endl;

		WriteToFile("test.xml");
	}

	void CameraTest::Render() {
		prog->Activate();
		prog->SetUniform("u_view_mat", MainCamera::Get()->view_mat());

		mesh_instance->Draw();
	}

}