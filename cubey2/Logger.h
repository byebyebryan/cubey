#pragma once

#include <iostream>
#include <time.h>
#include "GLFW/glfw3.h"
#include "ServiceLocator.h"

namespace cubey {

	class ILogger {
	public:
		virtual ~ILogger() {}
		virtual void Log(const std::string& message) = 0;
	};

	ILogger* Logger() {
		ServiceLocator<ILogger>::Get();
	}

	class ConsoleLogger : public ILogger {
	public:
		static void Init() {
			ServiceLocator<ILogger>::Set(new ConsoleLogger());
		}

		void Log(const std::string& message) override {
			time_t rawtime;
			struct tm * timeinfo;

			time(&rawtime);
			timeinfo = localtime(&rawtime);

			char buffer[80];
			strftime(buffer, 80, "%Y/%b/%d %X - ", timeinfo);

			std::cout << buffer << message.c_str() << std::endl;
		}
	};

	
}