#pragma once

#include <vector>

namespace cubey {
	std::vector<std::string> Tokenize(const std::string& str, const std::string& delimiters = ".");
}