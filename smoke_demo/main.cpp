
#include "SmokeDemo.h"

using namespace cubey;

int main(void) {
	Engine::Init();

	Engine::Push(new SmokeDemo());

	Engine::MainLoop();

}