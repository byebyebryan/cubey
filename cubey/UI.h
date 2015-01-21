#pragma once

#include "Interfaces.h"

namespace cubey {
	class UI : public IEngineEvents, public ISingleton<UI> {
	public:
		void Init() override;

	private:
		void UIRender() override;
		void Terminate() override;
	};
}


