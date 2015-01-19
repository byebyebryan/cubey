#pragma once

#include <iostream>
#include <functional>

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "AntTweakBar.h"

#include "Event.h"

namespace cubey {
	

	class System {
	public:
		static System* Main() {
			static System instance;
			return &instance;
		}
	private:
		System() {}
		
	};
}


