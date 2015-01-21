#pragma once

#include <functional>
#include <map>
#include <stdint.h>

namespace cubey {
	struct EmptyType {};

	template<typename T0 = EmptyType, typename T1 = EmptyType, typename T2 = EmptyType, typename T3 = EmptyType, typename T4 = EmptyType>
	struct GenericEvent {
		T0 para_0;
		T1 para_1;
		T2 para_2;
		T3 para_3;
		T4 para_4;
	};

	template<typename T>
	class EventLisenter {
	public:
		EventLisenter() {
			id_ = 0;
		}

		EventLisenter(const std::function<void(const T&)>& handler_func) : handler_func_(handler_func) {
			id_ = NewID();
		}

		~EventLisenter() {
			RemoveFromChannel();
		}

		void PushToChannel() {
			EventChannel<T>::Add(*this);
		}

		void RemoveFromChannel() {
			EventChannel<T>::Remove(id_);
		}

		static uint64_t NewID() {
			static uint64_t current_id = 0;
			return ++current_id;
		}

		uint64_t id_;
		std::function<void(const T&)> handler_func_;
	};

	template<typename T>
	class EventChannel {
	public:
		static void Add(const EventLisenter<T>& listener) {
			Listeners()[listener.id_] = listener.handler_func_;
		}

		static uint64_t DirtyAdd(const std::function<void(const T&)>& handler_func) {
			uint64_t dirty_id = EventLisenter<T>::NewID();
			Listeners()[dirty_id] = handler_func;
			return dirty_id;
		}

		static void Broadcast(const T& event) {
			if (!IsMuted()) {
				for (auto it = Listeners().rbegin(); it != Listeners().rend(); ++it) {
					it->second(event);
				}
			}
		}

		static void Broadcast(std::initializer_list<T> init_list) {
			if (!IsMuted()) {
				T e(init_list);
				Broadcast(e);
			}
		}

		static void Remove(uint64_t listener_id) {
			Listeners().erase(listener_id);
		}

		static void Mute() {
			IsMuted() = true;
		}

		static void Unmute() {
			IsMuted() = false;
		}

	private:
		static std::map<uint64_t, std::function<void(const T&)>>& Listeners() {
			static std::map<uint64_t, std::function<void(const T&)>> listeners_instance;
			return listeners_instance;
		}

		static bool& IsMuted() {
			static bool is_muted = false;
			return is_muted;
		}
	};
}