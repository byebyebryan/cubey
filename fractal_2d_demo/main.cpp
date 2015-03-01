
#include "CameraTest.h"

using namespace cubey;

int main(void) {
	Engine::Init();

	Engine::Push(new CameraTest());

	Engine::MainLoop();
}