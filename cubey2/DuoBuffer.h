#pragma once

#include <atomic>

namespace cubey {
	template<typename BufferT>
	class DuoBuffer {
	public:
		DuoBuffer() : 
			ping_(BufferT()), pong_(BufferT()), flip_(0) {

		}

		DuoBuffer(const BufferT& _buffer) : 
			ping_(_buffer), pong_(_buffer), flip_(0) {

		}

		DuoBuffer(const BufferT& _front_buffer, const BufferT& _back_buffer) :
			ping_(_front_buffer), pong_(_back_buffer), flip_(0) {

		}

		BufferT* front_buffer() {
			return flip_.load() == 0 ? &ping_ : &pong_;
		}

		BufferT* back_buffer() {
			return flip_.load() == 1 ? &ping_ : &pong_;
		}

		void Swap() {
			flip_.fetch_xor(1);
		}

	protected:
		BufferT ping_;
		BufferT pong_;

		std::atomic<int> flip_;
	};
}