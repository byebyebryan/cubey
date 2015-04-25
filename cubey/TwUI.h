#pragma once

#include "AntTweakBar.h"

#include "EngineEventsBase.h"
#include "SingletonBase.h"

#include <iostream>

namespace cubey {

	class TwUI : public EngineEventsBase, public SingletonBase<TwUI> {
	public:
		void SystemInit();

		void AddRW(const char* name, TwType type, void* var, const char* def, TwBar* bar = nullptr);
		void AddRO(const char* name, TwType type, void* var, const char* def, TwBar* bar = nullptr);

		/*template<typename T, void(T::*Fn)()>
		void AddButton(T* context, const char* name, const char* def, TwBar* bar = nullptr) {
			if (!bar) bar = main_bar_;
			TwAddButton(bar, name, &TwUI::TwButtonCall<T, Fn>, context, def);
		}*/

		void AddButton(const std::function<void()>& callback_fn, const char* name, const char* def, TwBar* bar = nullptr);

		void RemoveEntry(const char* name, TwBar* bar = nullptr);

		void ClearBar(TwBar* bar = nullptr);

		void AddDefaultInfo(TwBar* bar = nullptr);

		/*template<typename T, void (T::*Fn)()>
		static void TW_CALL TwButtonCall(void* client_data) {
			T* p = (T*)client_data;
			(p->*Fn)();
		}*/

		static void TW_CALL TwButtonCall(void* client_data) {
			uint32_t idx = (uint32_t)client_data;
			button_callbacks_[idx]();
		}

		TwBar* main_bar_;
	private:
		void UIRender() override;
		void Terminate() override;

		static std::map<uint32_t, std::function<void()>> button_callbacks_;
		static std::map<std::string, uint32_t> button_ids_;
	};
}


