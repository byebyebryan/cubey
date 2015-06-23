#pragma once

#include "LoggerUtil.h"

namespace cubey {

	class ConsoleLogger : public ILogger {
	public:
		static void Init() {
			ServiceLocator<ILogger>::Set(new ConsoleLogger());
		}

		void Log(const std::string& message) override {
			auto ss = LoggerUtil::GetPrefix();
			std::cout << ss << message.c_str() << std::endl;
		}
	};

	
}