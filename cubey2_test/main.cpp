
#include <thread>

#include "Logger.h"
#include "ServiceLocator.h"

using namespace cubey;

int main(void) {
	ServiceLocator<ILogger>::Set(new ConsoleLogger());
	ServiceLocator<ILogger>::Get()->Log("test");
	Log("test2");

	std::thread thread([]() {std::cout << "yeah!!";});
	thread.join();
}