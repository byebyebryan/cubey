#include "Time.h"

#include "GLFW/glfw3.h"

namespace cubey {
	const double kFPSLimit = 60.0;

	double Time::time_since_start_ = 0;
	double Time::raw_fps_ = 0;
	double Time::fps_limit_ = kFPSLimit;
	double Time::regulated_fps_ = kFPSLimit;
	double Time::target_delta_time_ = 1.0 / kFPSLimit;
	double Time::delta_time_ = 1.0 / kFPSLimit;
	double Time::frame_time_ = 0;
	double Time::logic_time_ = 0;
	double Time::render_time_ = 0;


	ScopeTimer::ScopeTimer(double* outside_timer) {
		outside_timer_ = outside_timer;
		internal_timer_ = glfwGetTime();
	}

	ScopeTimer::~ScopeTimer() {
		*outside_timer_ = glfwGetTime() - internal_timer_;
	}

}