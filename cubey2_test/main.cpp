
#include <thread>
#include <iostream>
#include <sstream>

#include "ConsoleLogger.h"
#include "ServiceLocator.h"

using namespace cubey;

void foo() {
	static int i = 0;
	i++;
	std::stringstream ss;
	ss << "Thread " << i << " " << std::this_thread::get_id();
	Log(ss);
}

int main(void) {
	ServiceLocator<ILogger>::Set(new ConsoleLogger());

	std::cout << std::this_thread::get_id() << std::endl;

	std::thread thread1([]() {
		foo(); });
	std::thread thread2([]() {
		foo(); });
	std::thread thread3([]() {
		foo(); });
	thread1.join();
	thread2.join();
	thread3.join();
}