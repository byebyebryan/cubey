#include "cubey.h"
#include "CameraTest.h"
#include "ParticleTest.h"

using namespace cubey;

int main(void) {
	Engine::Init();

	ParticleTest test;

	Engine::MainLoop();
}