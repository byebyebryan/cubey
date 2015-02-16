#pragma once

#include "AntTweakBar.h"

#include "EngineEventsBase.h"
#include "SingletonBase.h"

namespace cubey {
	class TwUI : public EngineEventsBase, public SingletonBase<TwUI> {
	public:
		void SystemInit();

		void AddRW(const char* name, TwType type, void* var, const char* def, TwBar* bar = nullptr) {
			if (!bar) bar = main_bar_;
			TwAddVarRW(bar, name, type, var, def);
		}
		void AddRO(const char* name, TwType type, void* var, const char* def, TwBar* bar = nullptr) {
			if (!bar) bar = main_bar_;
			TwAddVarRO(bar, name, type, var, def);
		}

		TwBar* main_bar_;
	private:
		void UIRender() override;
		void Terminate() override;
	};
}


