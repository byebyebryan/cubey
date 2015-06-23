#pragma once

#include <time.h>

#include "ILogger.h"

namespace cubey {

	void Log(const std::string& message);

	void Log(const std::stringstream& message);

	class LoggerUtil {
	public:
		static std::string GetPrefix();
	};

}
