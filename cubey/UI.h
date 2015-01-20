#pragma once

#include "System.h"

namespace cubey {
	class UI : public System<UI>
	{
	public:
		void Init() override;

	private:
		void UIRender() override;
		void Terminate() override;
	};
}


