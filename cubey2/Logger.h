#pragma once

#include <iostream>
#include <time.h>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "GLFW/glfw3.h"
#include "ServiceLocator.h"

namespace cubey {

	class ILogger {
	public:
		virtual ~ILogger() {}
		virtual void Log(const std::string& message) = 0;
	};

	ILogger* Logger() {
		return ServiceLocator<ILogger>::Get();
	}

	void Log(const std::string& message) {
		Logger()->Log(message);
	}

	class LoggerMT {
		static std::mutex process_mutex_;
		static std::condition_variable process_cond_var_;

		static std::queue<std::string> msg_queue_;
	};

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
	protected:
		std::mutex process_mutex_;
		std::condition_variable process_cond_var_;

		std::queue<std::string> msg_queue_;
	};

	
}