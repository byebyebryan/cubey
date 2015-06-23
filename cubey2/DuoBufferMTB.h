#pragma once

#include <thread>
#include <mutex>

#include "DuoBuffer.h"

namespace cubey {
	template<typename BufferT>
	class DuoBufferMTB : public DuoBuffer<BufferT> {
	public:
		std::unique_lock<std::mutex> LockFrontBuffer() {
			std::unique_lock<std::mutex> lock(front_buffer_mutex_);
		}

		std::unique_lock<std::mutex> LockBackBuffer() {
			std::unique_lock<std::mutex> lock(back_buffer_mutex_);
		}

		void SwapMTB() {
			std::unique_lock<std::mutex> swap_lock(swap_mutex_, std::defer_lock);
			swap_lock.lock();
			if(!has_requested_swap_) {
				has_requested_swap_ = true;
			}
			swap_lock.unlock();
			auto f_lock = LockFrontBuffer();
			auto b_lock = LockBackBuffer();
			swap_lock.lock();
			if (is_waiting_for_swap_) {
				Swap();
				is_waiting_for_swap_ = false;
			}
			swap_lock.unlock();
			swap_cond_var_.notify_all();
		}
	protected:
		std::atomic<bool> has_finished_swap_;
		std::atomic<bool> has_requested_swap_;
		std::atomic<bool> is_waiting_for_swap_;

		std::atomic_flag has_swap_been_requested_;
		std::atomic_flag has_swap_been_finished_;

		std::mutex front_buffer_mutex_;
		std::mutex back_buffer_mutex_;

		std::mutex swap_mutex_;
		std::condition_variable swap_cond_var_;

		std::mutex front_buffer_cond_var_;
		std::mutex back_buffer_cond_var_;
	};
}