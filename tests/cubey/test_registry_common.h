#pragma once

#include "test_registry.h"

#include <array>

#define CUBEY_TEST(test_function)                                                                  \
    TestCase {                                                                                     \
        #test_function, &test_function                                                             \
    }
