
#include "FractalDemo.h"

using namespace cubey;

int main(void) {
	Engine::Init();

	Engine::Push(new FractalDemo());

	Engine::MainLoop();
}