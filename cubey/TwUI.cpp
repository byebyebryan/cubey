#include "TwUI.h"

#include "GLFW/glfw3.h"
#include "Input.h"
#include "Camera.h"

namespace cubey {

	void TwUI::SystemInit() {
		PushToEngine();

		TwInit(TW_OPENGL_CORE, NULL);
		EventChannel<Engine::WindowSizeEvent>::DirtyAdd([](const Engine::WindowSizeEvent& e){
			TwWindowSize(e.width, e.height); 
		});
		EventChannel<Engine::MouseButtonEvent>::DirtyAdd([](const Engine::MouseButtonEvent& e){
			Input::Get()->mouse_button_consumed_by_ui_ = TwEventMouseButtonGLFW(e.button, e.action);
		});
		EventChannel<Engine::MousePosEvent>::DirtyAdd([](const Engine::MousePosEvent& e){
			TwEventMousePosGLFW(e.xpos, e.ypos); 
		});
		EventChannel<Engine::MouseWheelEvent>::DirtyAdd([](const Engine::MouseWheelEvent& e){
			Input::Get()->mouse_wheel_consumed_by_ui_ = TwEventMouseWheelGLFW(e.yoffset);
		});
		EventChannel<Engine::KeyEvent>::DirtyAdd([](const Engine::KeyEvent& e){
			TwEventKeyGLFW(e.key, e.action); 
		});
		EventChannel<Engine::CharEvent>::DirtyAdd([](const Engine::CharEvent& e){
			TwEventCharGLFW(e.codepoint, GLFW_PRESS); 
		});

		main_bar_ = TwNewBar("main");
		int w, h;
		glfwGetWindowSize(Engine::window_, &w, &h);
		TwWindowSize(w, h);
		TwDefine(" GLOBAL help='cubey' ");
		TwDefine("main size='320 640' refresh=0.5 ");

		//TwAddVarRO(tw_bar_, "time", TW_TYPE_DOUBLE, &Time::time_since_start_, "precision=2 help='Time Since Start.' group=Time");
		//TwAddVarRO(main_bar_, "frame time", TW_TYPE_DOUBLE, &Time::frame_time_, "precision=4 group=Time");
		//TwAddVarRO(main_bar_, "fps", TW_TYPE_DOUBLE, &Time::raw_fps_, "precision=2 group=Time");
		
		//TwAddVarRO(main_bar_, "delta time", TW_TYPE_DOUBLE, &Time::delta_time_, "precision=4 group=Time");
		//TwAddVarRO(main_bar_, "regulated fps", TW_TYPE_DOUBLE, &Time::regulated_fps_, "precision=2 group=Time");

		//TwAddVarRO(main_bar_, "camera position", TW_TYPE_DIR3F, &MainCamera::Get()->transform_.position_, "group=Camera");
		//TwAddVarRO(main_bar_, "camera orientation", TW_TYPE_QUAT4F, &MainCamera::Get()->transform_.orientation_, "group=Camera");
		//TwAddVarRO(bar, "camera euler angles", TW_TYPE_DIR3F, &Camera::Main()->transform_.euler_angles_, "");

		//TwAddVarRO(bar, "camera forward", TW_TYPE_DIR3F, &Camera::Main()->transform_.forward_, "");
		//TwAddVarRO(bar, "camera right", TW_TYPE_DIR3F, &Camera::Main()->transform_.right_, "");
		//TwAddVarRO(bar, "camera up", TW_TYPE_DIR3F, &Camera::Main()->transform_.up_, "");

		//TwAddVarRO(bar, "camera target", TW_TYPE_DIR3F, &Camera::Main()->look_at_target_pos_, "");
		//TwAddVarRO(bar, "camera yaw", TW_TYPE_FLOAT, &Camera::Main()->yaw_, "");
		//TwAddVarRO(bar, "camera pitch", TW_TYPE_FLOAT, &Camera::Main()->pitch_, "");
		//TwAddVarRO(bar, "camera roll", TW_TYPE_FLOAT, &Camera::Main()->roll_, "");
	}

	void TwUI::AddRW(const char* name, TwType type, void* var, const char* def, TwBar* bar /*= nullptr*/) {
		if (!bar) bar = main_bar_;
		TwAddVarRW(bar, name, type, var, def);
	}

	void TwUI::AddRO(const char* name, TwType type, void* var, const char* def, TwBar* bar /*= nullptr*/) {
		if (!bar) bar = main_bar_;
		TwAddVarRO(bar, name, type, var, def);
	}

	void TwUI::AddButton(const std::function<void()>& callback_fn, const char* name, const char* def, TwBar* bar /*= nullptr*/) {
		if (!bar) bar = main_bar_;

		static uint32_t idx = 0;
		++idx;

		button_callbacks_[idx] = callback_fn;
		button_ids_[name] = idx;

		TwAddButton(bar, name, &TwUI::TwButtonCall, (void*)idx, def);
	}

	void TwUI::RemoveEntry(const char* name, TwBar* bar /*= nullptr*/) {
		if (!bar) bar = main_bar_;
		TwRemoveVar(bar, name);

		auto it = button_ids_.find(name);
		if (it != button_ids_.end()) {
			int idx = it->second;
			button_ids_.erase(it);
			button_callbacks_.erase(idx);
		}
	}

	void TwUI::ClearBar(TwBar* bar /*= nullptr*/) {
		if (!bar) bar = main_bar_;
		TwRemoveAllVars(bar);

		button_callbacks_.clear();
		button_ids_.clear();
	}

	void TwUI::AddDefaultInfo(TwBar* bar /*= nullptr*/) {
		if (!bar) bar = main_bar_;

		TwAddVarRO(bar, "frame time", TW_TYPE_DOUBLE, &Time::frame_time_, "precision=4 group=Time");
		TwAddVarRO(bar, "fps", TW_TYPE_DOUBLE, &Time::raw_fps_, "precision=2 group=Time");

		TwAddVarRO(bar, "delta time", TW_TYPE_DOUBLE, &Time::delta_time_, "precision=4 group=Time");
		TwAddVarRO(bar, "regulated fps", TW_TYPE_DOUBLE, &Time::regulated_fps_, "precision=2 group=Time");
	}

	void TwUI::UIRender() {
		TwDraw();
	}

	void TwUI::Terminate() {
		TwTerminate();
	}

	std::map<uint32_t, std::function<void()>> TwUI::button_callbacks_;

	std::map<std::string, uint32_t> TwUI::button_ids_;

}