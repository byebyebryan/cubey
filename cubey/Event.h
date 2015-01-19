#pragma once

#include <functional>
#include <vector>

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
	class EventChannel {
	public:
		static int Add(const std::function<void(const T&)>& handler) {
			handlers().push_back(handler);
			return handlers().size() - 1;
		}

		static void Broadcast(const T& event) {
			for (auto& handler : handlers()) {
				handler(event);
			}
		}

		static void Boradcast(std::initializer_list<T> init_list) {
			T e(init_list);
			Boradcast(e);
		}

	private:
		static std::vector<std::function<void(const T&)>>& handlers() {
			static std::vector<std::function<void(const T&)>> handlers_instance;
			return handlers_instance;
		}
	};
}