#pragma once

namespace cubey {
	template<typename ServiceT> 
	class ServiceLocator {
	public:
		static ServiceT* Get() {
			ServiceT* p = service_internal();
			return p;
		}

		static void Set(ServiceT* new_service) {
			if (service_internal() && service_internal() != new_service) {
				delete service_internal();
			}
			service_internal() = new_service;
		}

	private:
		static ServiceT*& service_internal() {
			static ServiceT* service = nullptr;
			return service;
		}
	};
}