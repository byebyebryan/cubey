#pragma once

#include <iostream>

#include <sstream>

#include "ServiceLocator.h"

namespace cubey {

	class ILogger {
	public:
		virtual ~ILogger() {}
		virtual void Log(const std::string& message) = 0;
	};

	
}