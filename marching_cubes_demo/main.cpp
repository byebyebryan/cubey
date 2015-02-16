
#include "MarchingCubesDemo.h"

using namespace cubey;

int main(void) {
	Engine::Init();

	Engine::Push(new MarchingCubesDemo());

	Engine::MainLoop();

}