#pragma once

#include "Interfaces.h"
#include "AntTweakBar.h"

namespace cubey {
	class UI : public IEngineEvents, public ISingleton<UI> {
	public:
		void SystemInit() override;

		TwBar* tw_bar_;
	private:
		void UIRender() override;
		void Terminate() override;
	};
}


