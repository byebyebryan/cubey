#pragma once

#include <thread>
#include <mutex>

#include "DuoBuffer.h"

namespace cubey {
	template<typename BufferT>
	class DuoBufferMTNB : public DuoBuffer<BufferT> {
	public:
		DuoBufferMTNB() :
			front_buffer_lock_(std::unique_lock<std::mutex>(front_buffer_mutex_, std::defer_lock)),
			back_buffer_lock_(std::unique_lock<std::mutex>(back_buffer_mutex_, std::defer_lock)) {

		}

		bool TryLockFrontBuffer() {
			if (front_buffer_lock_.try_lock()) {
				front_buffer_owener_id_ = std::this_thread::get_id();
				return true;
			}
			return false;
		}

		void UnlockFrontBuffer() {
			if (std::this_thread::get_id() == front_buffer_owener_id_) {
				front_buffer_lock_.unlock();
			}
		}

		bool TryLockBackBuffer() {
			if (back_buffer_lock_.try_lock()) {
				back_buffer_owener_id_ = std::this_thread::get_id();
				return true;
			}
			return false;
		}

		void UnlockBackBuffer() {
			if (std::this_thread::get_id() == back_buffer_owener_id_) {
				back_buffer_lock_.unlock();
			}
		}

		bool TrySwap() {
			if (TryLockBackBuffer()) {
				if (TryLockFrontBuffer()) {
					Swap();
					UnlockFrontBuffer();
					UnlockBackBuffer();
					return true;
				}
				else {
					UnlockBackBuffer();
					return false;
				}
			}
			return false;
		}

	protected:
		std::mutex front_buffer_mutex_;
		std::mutex back_buffer_mutex_;

		std::unique_lock<std::mutex> front_buffer_lock_;
		std::unique_lock<std::mutex> back_buffer_lock_;

		std::thread::id front_buffer_owener_id_;
		std::thread::id back_buffer_owener_id_;
	};
}