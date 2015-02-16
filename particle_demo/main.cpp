
#include "ParticleDemo.h"

using namespace cubey;

int main(void) {
	Engine::Init();

	Engine::Push(new ParticleDemo());

	Engine::MainLoop();

}