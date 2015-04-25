#pragma once

#include "cubey.h"
#include "Fractal2DDemo.h"

namespace cubey {
	class MainMenu : public EngineEventsBase, public SingletonBase<MainMenu> {
	public:
		void Init() override;

		void ShowMenu();
		void HideMenu();

		void AddReturnToMenuButton(EngineEventsBase* scene);
	private:
		Fractal2DDemo* fractal_2d_demo_;

		TwBar* menu_bar_;
	};

}


