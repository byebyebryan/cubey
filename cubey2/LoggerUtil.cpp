#include "LoggerUtil.h"

namespace cubey {

	void Log(const std::string& message) {
		ServiceLocator<ILogger>::Get()->Log(message);
	}

	void Log(const std::stringstream& message) {
		ServiceLocator<ILogger>::Get()->Log(message.str());
	}

	std::string LoggerUtil::GetPrefix() {
		time_t rawtime;
		struct tm timeinfo;

		time(&rawtime);
		localtime_s(&timeinfo, &rawtime);

		char buffer[80];
		strftime(buffer, 80, "%Y/%b/%d %X - ", &timeinfo);

		return std::string(buffer);
	}
}