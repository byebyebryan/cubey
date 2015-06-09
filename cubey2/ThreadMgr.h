#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>

namespace cubey {
	class ThreadMgr {
	public:
		static std::mutex logger_mutex_;

	};
}