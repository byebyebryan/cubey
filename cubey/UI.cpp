#include "UI.h"

#include "AntTweakBar.h"
#include "Engine.h"
#include "Camera.h"
#include "Time.h"

namespace cubey {

	void UI::Init() {
		System::Init();

		TwInit(TW_OPENGL, NULL);
		EventChannel<Engine::WindowSizeEvent>::Add([](const Engine::WindowSizeEvent& e){TwWindowSize(e.width, e.height); });
		EventChannel<Engine::MouseButtonEvent>::Add([](const Engine::MouseButtonEvent& e){TwEventMouseButtonGLFW(e.button, e.action); });
		EventChannel<Engine::MousePosEvent>::Add([](const Engine::MousePosEvent& e){TwEventMousePosGLFW(e.xpos, e.ypos); });
		EventChannel<Engine::MouseWheelEvent>::Add([](const Engine::MouseWheelEvent& e){TwEventMouseWheelGLFW(e.yoffset); });
		EventChannel<Engine::KeyEvent>::Add([](const Engine::KeyEvent& e){TwEventKeyGLFW(e.key, e.action); });
		EventChannel<Engine::CharEvent>::Add([](const Engine::CharEvent& e){TwEventCharGLFW(e.codepoint, GLFW_PRESS); });

		TwBar *bar = TwNewBar("TweakBar");
		int w, h;
		TwWindowSize(800, 600);
		TwDefine(" GLOBAL help='AntTweakBar Test' ");

		TwAddVarRO(bar, "time", TW_TYPE_DOUBLE, &Time::time_since_start_, "precision=2 help='Time Since Start.' ");
		TwAddVarRO(bar, "frame time", TW_TYPE_DOUBLE, &Time::frame_time_, "precision=4 help='Frame Time.' ");
		TwAddVarRO(bar, "fps", TW_TYPE_DOUBLE, &Time::raw_fps_, "precision=2 help='Frame Per Second' ");
		
		TwAddVarRO(bar, "delta time", TW_TYPE_DOUBLE, &Time::delta_time_, "precision=4 help='Delta Time.' ");
		TwAddVarRO(bar, "regulated fps", TW_TYPE_DOUBLE, &Time::regulated_fps_, "precision=2 help='Regulated FPS.' ");

		TwAddVarRO(bar, "camera position", TW_TYPE_DIR3F, &Camera::Main()->position_, "");
		TwAddVarRO(bar, "camera orientation", TW_TYPE_QUAT4F, &Camera::Main()->orientation_, "");
		TwAddVarRO(bar, "camera target", TW_TYPE_DIR3F, &Camera::Main()->look_at_, "");
	}

	void UI::UIRender() {
		TwDraw();
	}

	void UI::Terminate() {
		TwTerminate();
	}

}