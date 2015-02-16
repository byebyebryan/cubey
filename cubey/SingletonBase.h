#pragma once

namespace cubey {

	template<typename T>
	class SingletonBase {
	public:
		virtual ~SingletonBase() { 
			delete Get(); 
		}
		static T* Get() {
			static T* instance = new T();
			return instance;
		}
	protected:
		SingletonBase() {}
	};
}