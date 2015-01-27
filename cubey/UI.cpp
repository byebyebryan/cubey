#include "UI.h"

#include "GLFW/glfw3.h"
#include "AntTweakBar.h"
#include "Camera.h"

namespace cubey {

	void UI::Init() {
		TwInit(TW_OPENGL, NULL);
		EventChannel<Engine::WindowSizeEvent>::DirtyAdd([](const Engine::WindowSizeEvent& e){
			TwWindowSize(e.width, e.height); 
		});
		EventChannel<Engine::MouseButtonEvent>::DirtyAdd([](const Engine::MouseButtonEvent& e){
			TwEventMouseButtonGLFW(e.button, e.action); 
		});
		EventChannel<Engine::MousePosEvent>::DirtyAdd([](const Engine::MousePosEvent& e){
			TwEventMousePosGLFW(e.xpos, e.ypos); 
		});
		EventChannel<Engine::MouseWheelEvent>::DirtyAdd([](const Engine::MouseWheelEvent& e){
			TwEventMouseWheelGLFW(e.yoffset); 
		});
		EventChannel<Engine::KeyEvent>::DirtyAdd([](const Engine::KeyEvent& e){
			TwEventKeyGLFW(e.key, e.action); 
		});
		EventChannel<Engine::CharEvent>::DirtyAdd([](const Engine::CharEvent& e){
			TwEventCharGLFW(e.codepoint, GLFW_PRESS); 
		});

		TwBar *bar = TwNewBar("TweakBar");
		int w, h;
		glfwGetWindowSize(Engine::window_, &w, &h);
		TwWindowSize(w, h);
		TwDefine(" GLOBAL help='AntTweakBar Test' ");

		TwAddVarRO(bar, "time", TW_TYPE_DOUBLE, &Time::time_since_start_, "precision=2 help='Time Since Start.' ");
		TwAddVarRO(bar, "frame time", TW_TYPE_DOUBLE, &Time::frame_time_, "precision=4 help='Frame Time.' ");
		TwAddVarRO(bar, "fps", TW_TYPE_DOUBLE, &Time::raw_fps_, "precision=2 help='Frame Per Second' ");
		
		TwAddVarRO(bar, "delta time", TW_TYPE_DOUBLE, &Time::delta_time_, "precision=4 help='Delta Time.' ");
		TwAddVarRO(bar, "regulated fps", TW_TYPE_DOUBLE, &Time::regulated_fps_, "precision=2 help='Regulated FPS.' ");

		TwAddVarRO(bar, "camera position", TW_TYPE_DIR3F, &Camera::Main()->transform_.position_, "");
		TwAddVarRO(bar, "camera orientation", TW_TYPE_QUAT4F, &Camera::Main()->transform_.orientation_, "");
		//TwAddVarRO(bar, "camera euler angles", TW_TYPE_DIR3F, &Camera::Main()->transform_.euler_angles_, "");

		//TwAddVarRO(bar, "camera forward", TW_TYPE_DIR3F, &Camera::Main()->transform_.forward_, "");
		//TwAddVarRO(bar, "camera right", TW_TYPE_DIR3F, &Camera::Main()->transform_.right_, "");
		//TwAddVarRO(bar, "camera up", TW_TYPE_DIR3F, &Camera::Main()->transform_.up_, "");

		//TwAddVarRO(bar, "camera target", TW_TYPE_DIR3F, &Camera::Main()->look_at_target_pos_, "");
		//TwAddVarRO(bar, "camera yaw", TW_TYPE_FLOAT, &Camera::Main()->yaw_, "");
		//TwAddVarRO(bar, "camera pitch", TW_TYPE_FLOAT, &Camera::Main()->pitch_, "");
		//TwAddVarRO(bar, "camera roll", TW_TYPE_FLOAT, &Camera::Main()->roll_, "");
	}

	void UI::UIRender() {
		TwDraw();
	}

	void UI::Terminate() {
		TwTerminate();
	}

}