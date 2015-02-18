#pragma once

#include "cubey.h"

namespace cubey {
	class CameraTest : public EngineEventsBase, public AutoXmlBase<CameraTest> {
	public:
		void Init() override;
		void Render() override;

	private:
		ShaderProgram* prog;
		Mesh* mesh;
		MeshInstance* mesh_instance;

		AUTO_XML_VAR_TW(glm::vec3, foo, glm::vec3(1,0,0), "group=test");
		AUTO_XML_VAR_TW(float, bar, 12.0f, "group=test");
	};

}


