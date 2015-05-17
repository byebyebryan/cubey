#include "MainMenu.h"

#include "ObjLoader.h"
#include "glm/gtx/transform2.hpp"

namespace cubey {

	void MainMenu::Init() {
		menu_bar_ = TwNewBar("menu");
		//TwDefine("main visible=false");

		TwUI::Get()->AddButton([this](){
			HideMenu();
			fractal_2d_demo_ = new Fractal2DDemo();
			fractal_2d_demo_->StartUp();
		}, "fractal demo", "group=MainMenu", menu_bar_);

		ShowMenu();
	}

	void MainMenu::ShowMenu() {
		TwDefine("main visible=false");
		TwDefine("menu visible=true");
	}

	void MainMenu::HideMenu() {
		TwDefine("main visible=true");
		TwDefine("menu visible=false");
	}

	void MainMenu::AddReturnToMenuButton(EngineEventsBase* scene) {
		TwUI::Get()->AddButton([scene, this](){
			scene->CloseDown(); 
			delete scene; 
			TwUI::Get()->ClearBar(); 
			MainMenu::Get()->ShowMenu();
			}, "return", "group=MainMenu");
	}

}