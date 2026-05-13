#include "test_registry_domains.h"

#include <span>
#include <vector>

namespace cubey::tests {
namespace {

void append_cases(std::vector<TestCase>& tests, std::span<const TestCase> source) {
    tests.insert(tests.end(), source.begin(), source.end());
}

} // namespace

std::span<const TestCase> core_tests() {
    static const std::vector<TestCase> tests = [] {
        std::vector<TestCase> result;
        append_cases(result, core_test_cases());
        append_cases(result, asset_test_cases());
        append_cases(result, engine_host_input_test_cases());
        append_cases(result, vulkan_test_cases());
        append_cases(result, render_test_cases());
        append_cases(result, scene_test_cases());
        return result;
    }();
    return tests;
}

} // namespace cubey::tests
