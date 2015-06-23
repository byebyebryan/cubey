#pragma once

#include <thread>
#include <mutex>

#include "DuoBuffer.h"

namespace cubey {
	template<typename BufferT>
	class DuoBufferMTB : public DuoBuffer<BufferT> {
	public:
		void LockFrontBuffer() {
			//If another thread has already requested a swap then wait for it to finish first.
			if (has_requested_swap_) {
				WaitForSwap();
			}
			int index = ++front_buffer_queued_index_;
			front_buffer_cond_var_.wait(front_buffer_mutex_, [&] {return index == front_buffer_current_index_; });
		}

		void UnlockFrontBuffer() {
			front_buffer_current_index_++;
			front_buffer_cond_var_.notify_all();
		}

		void LockBackBuffer() {
			//If another thread has already requested a swap then wait for it to finish first.
			if (has_requested_swap_) {
				WaitForSwap();
			}
			int index = ++back_buffer_queued_index_;
			back_buffer_cond_var_.wait(back_buffer_mutex_, [&] {return index == back_buffer_current_index_; });
		}

		void UnlockBackBuffer() {
			back_buffer_current_index_++;
			back_buffer_cond_var_.notify_all();
		}

		void WaitForSwap() {
			int index = ++swap_queued_index_;
			swap_cond_var_.wait(swap_mutex_, [&] {return has_finished_swap_; });
			swap_queued_index_--;
			post_swap_cond_var_.notify_all();
		}

		void SwapBlock() {
			//Only the first attempt would proceed into swap.
			if (!has_requested_swap_) {
				has_requested_swap_ = true;

				//Wait for the queued requests for the back buffer to finish.
				int b_index = ++back_buffer_queued_index_;
				back_buffer_cond_var_.wait(back_buffer_mutex_, [&] {return b_index == back_buffer_current_index_; });

				//Wait for the queued requests for the front buffer to finish.
				int f_index = ++front_buffer_queued_index_;
				front_buffer_cond_var_.wait(front_buffer_mutex_, [&] {return f_index == front_buffer_current_index_; });

				Swap();

				//Now we notify all the threads waiting for swap.
				has_finished_swap_ = true;
				swap_cond_var_.notify_all();
				post_swap_cond_var_.wait(swap_mutex_, [&] {return swap_queued_index_ == 0; });

				//Reset.
				has_requested_swap_ = false;
				has_finished_swap_ = false;

				front_buffer_current_index_ = 1;
				front_buffer_queued_index_ = 0;
				back_buffer_current_index_ = 1;
				back_buffer_queued_index_ = 0;
			}
			//Following attempts would just wait for the swap.
			else {
				WaitForSwap();
			}
		}
	protected:
		bool has_finished_swap_;
		bool has_requested_swap_;

		int front_buffer_queued_index_;
		int front_buffer_current_index_;
		int back_buffer_queued_index_;
		int back_buffer_current_index_;

		std::mutex front_buffer_mutex_;
		std::mutex back_buffer_mutex_;
		std::condition_variable front_buffer_cond_var_;
		std::condition_variable back_buffer_cond_var_;

		int swap_queued_index_;

		std::mutex swap_mutex_;
		std::condition_variable swap_cond_var_;
		std::condition_variable post_swap_cond_var_;
	};
}