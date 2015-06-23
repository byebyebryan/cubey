#pragma once

namespace cubey {
	template<typename BufferT>
	class DuoBuffer {
	public:
		DuoBuffer() : 
			ping_(BufferT()), pong_(BufferT()), flip_(false) {
		}

		DuoBuffer(const BufferT& _buffer) : 
			ping_(_buffer), pong_(_buffer), flip_(false) {
		}

		DuoBuffer(const BufferT& _front_buffer, const BufferT& _back_buffer) :
			ping_(_front_buffer), pong_(_back_buffer), flip_(false) {
		}

		BufferT* front_buffer() {
			return flip_ ? &ping_ : &pong_;
		}

		BufferT* back_buffer() {
			return flip_ ? &pong_ : &ping_;
		}

		void Swap() {
			flip_ = !flip_;
		}

	protected:
		BufferT ping_;
		BufferT pong_;

		BufferT* front_buffer_ptr_;
		BufferT* back_buffer_ptr_;

		bool flip_;
	};
}