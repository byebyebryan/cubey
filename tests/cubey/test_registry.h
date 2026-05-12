#pragma once

#include <span>

namespace cubey::tests {

struct TestCase {
    const char* name = "";
    void (*run)() = nullptr;
};

[[nodiscard]] std::span<const TestCase> core_tests();

} // namespace cubey::tests
