#include "test_registry.h"

#include <cstdio>
#include <exception>

int main() {
    const char* active_test = nullptr;

    try {
        for (const cubey::tests::TestCase& test : cubey::tests::core_tests()) {
            active_test = test.name;
            test.run();
        }
    } catch (const std::exception& error) {
        if (active_test != nullptr) {
            std::fprintf(stderr, "cubey_core_tests: %s failed: %s\n", active_test, error.what());
        } else {
            std::fprintf(stderr, "cubey_core_tests: %s\n", error.what());
        }
        return 1;
    }

    return 0;
}
