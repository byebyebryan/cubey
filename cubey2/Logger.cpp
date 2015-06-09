#include "Logger.h"

namespace cubey {
	std::mutex LoggerMT::process_mutex_;

	std::condition_variable LoggerMT::process_cond_var_;

	std::queue<std::string> LoggerMT::msg_queue_;

}



