#pragma once

#include <atomic>
#include <mutex>
#include <thread>

namespace cubey {
	template<typename BufferT>
	class DuoBuffer {
	public:
		DuoBuffer() : 
			ping_(BufferT()), pong_(BufferT()), flip_(false) {
			Refresh();
		}

		DuoBuffer(const BufferT& _buffer) : 
			ping_(_buffer), pong_(_buffer), flip_(false) {
			Refresh();
		}

		DuoBuffer(const BufferT& _front_buffer, const BufferT& _back_buffer) :
			ping_(_front_buffer), pong_(_back_buffer), flip_(false) {
			Refresh();
		}

		BufferT* front_buffer() {
			return front_buffer_ptr_;
		}

		BufferT* back_buffer() {
			return back_buffer_ptr_;
		}

		void Swap() {
			flip_ = !flip_;
			Refresh();
		}
		
		void Refresh() {
			front_buffer_ptr_ = flip_ ? &ping_ : &pong_;
			back_buffer_ptr_ = flip_ ? &pong_ : &ping_;
		}

	protected:
		BufferT ping_;
		BufferT pong_;

		BufferT* front_buffer_ptr_;
		BufferT* back_buffer_ptr_;

		bool flip_;
	};

	template<typename BufferT>
	class DuoBufferMT : public DuoBuffer<BufferT> {
	public:
		void LockFrontBuffer() {
			std::unique_lock<std::mutex> lock(front_buffer_mutex_, std::defer_lock);


		}

		void UnlockFrontBuffer() {

		}

		void LockBackBuffer() {

		}
	protected:
		std::atomic_flag has_requested_swap_;
		std::atomic_flag is_waiting_for_swap_;

		std::mutex front_buffer_mutex_;
		std::mutex back_buffer_mutex_;

		std::mutex front_buffer_cond_var_;
		std::mutex back_buffer_cond_var_;
	};
}