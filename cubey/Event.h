#pragma once

#include <functional>
#include <vector>

//#define DECLARE_EVENT_TYPE0(event_name) \
//	struct event_name : public Event<> { \
//	};
//
//#define DECLARE_EVENT_TYPE1(event_name, type_0) \
//	struct event_name : public Event<type_0> { \
//		event_name(type_0 p_0) : Event(p_0) {} \
//	};
//
//#define DECLARE_EVENT_TYPE2(event_name, type_0, type_1) \
//	struct event_name : public Event<type_0, type_1> { \
//		event_name(type_0 p_0, type_1 p_1) : Event(p_0, p_1) {} \
//	};
//
//#define DECLARE_EVENT_TYPE3(event_name, type_0, type_1, type_2) \
//	struct event_name : public Event<type_0, type_1, type_2> { \
//		event_name(type_0 p_0, type_1 p_1, type_2 p_2) : Event(p_0, p_1, p_2) {} \
//	};
//
//#define DECLARE_EVENT_TYPE4(event_name, type_0, type_1, type_2, type_3) \
//	struct event_name : public Event<type_0, type_1, type_2, type_3> { \
//		event_name(type_0 p_0, type_1 p_1, type_2 p_2, type_3 p_3) : Event(p_0, p_1, p_2, p_3) {} \
//	};
//
//#define DECLARE_EVENT_TYPE5(event_name, type_0, type_1, type_2, type_3, type_4) \
//	struct event_name : public Event<type_0, type_1, type_2, type_3, type_4> { \
//		event_name(type_0 p_0, type_1 p_1, type_2 p_2, type_3 p_3, type_4 p_4) : Event(p_0, p_1, p_2, p_3, p_4) {} \
//	};

namespace cubey {
	struct Event {};

	struct EmptyType {};

	template<typename T0 = EmptyType, typename T1 = EmptyType, typename T2 = EmptyType, typename T3 = EmptyType, typename T4 = EmptyType>
	struct GenericEvent {
		T0 para_0;
		T1 para_1;
		T2 para_2;
		T3 para_3;
		T4 para_4;
		/*GenericEvent() {}
		GenericEvent(const T0& p_0) : para_0(p_0) {}
		GenericEvent(const T0& p_0, const T1& p_1) : para_0(p_0), para_1(p_1) {}
		GenericEvent(const T0& p_0, const T1& p_1, const T2& p_2) : para_0(p_0), para_1(p_1), para_2(p_2) {}
		GenericEvent(const T0& p_0, const T1& p_1, const T2& p_2, const T3& p_3) : para_0(p_0), para_1(p_1), para_2(p_2), para_3(p_3) {}
		GenericEvent(const T0& p_0, const T1& p_1, const T2& p_2, const T3& p_3, const T4& p_4) : para_0(p_0), para_1(p_1), para_2(p_2), para_3(p_3), para_4(p_4) {}*/
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
	private:
		static std::vector<std::function<void(const T&)>>& handlers() {
			static std::vector<std::function<void(const T&)>> handlers_instance;
			return handlers_instance;
		}
	};
}