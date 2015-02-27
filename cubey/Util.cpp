#include "Util.h"

namespace cubey {
	std::vector<std::string> Tokenize(const std::string& str, const std::string& delimiters /*= "."*/) {
		std::vector<std::string> tokens;
		std::string::size_type last_pos = str.find_first_not_of(delimiters);
		std::string::size_type pos = str.find_first_of(delimiters, last_pos);

		while (pos != std::string::npos || last_pos != std::string::npos) {
			tokens.push_back(str.substr(last_pos, pos - last_pos));
			last_pos = str.find_first_not_of(delimiters, pos);
			pos = str.find_first_of(delimiters, last_pos);
		}
		return tokens;
	}

}