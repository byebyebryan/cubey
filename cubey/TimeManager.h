#pragma once

namespace cubey {
	class Time
	{
	public:
		static double time_since_start() { return time_since_start_; }
		static double raw_fps() { return raw_fps_; }
		static double regulated_fps() {return regulated_fps_;}
		static double delta_time() {return delta_time_;}
		static double frame_time() {return frame_time_;}
		static double logic_time() {return logic_time_;}
		static double render_time() {return render_time_;}
	
	private:
		static double time_since_start_;
		static double raw_fps_;
		static double fps_limit_;
		static double regulated_fps_;
		static double target_delta_time_;
		static double delta_time_;
		static double frame_time_;
		static double logic_time_;
		static double render_time_;

		friend class Engine;
		friend class TwUI;
		friend class ScopeTimer;
	};

	class ScopeTimer {
	public:
		ScopeTimer(double* outside_timer);
		~ScopeTimer();
	private:
		double* outside_timer_;
		double internal_timer_;
	};
}


