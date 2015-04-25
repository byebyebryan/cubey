
#include "MainMenu.h"

using namespace cubey;

int main(void) {
	Engine::Init();

	Engine::Push(MainMenu::Get());

	Engine::MainLoop();
}