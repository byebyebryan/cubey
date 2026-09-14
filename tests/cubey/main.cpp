#include "test_registry.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool matches_any_filter(std::string_view test_name,
                                      const std::vector<std::string_view>& filters) {
    for (const std::string_view filter : filters) {
        if (test_name.find(filter) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> filters;
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) != "--filter") {
            std::fprintf(stderr, "cubey_core_tests: expected --filter <substring>\n");
            return 2;
        }
        if (++index == argc) {
            std::fprintf(stderr, "cubey_core_tests: --filter requires a non-empty substring\n");
            return 2;
        }
        const std::string_view filter(argv[index]);
        if (filter.empty() || filter.starts_with("--")) {
            std::fprintf(stderr, "cubey_core_tests: --filter requires a non-empty substring\n");
            return 2;
        }
        filters.emplace_back(filter);
    }

    const char* active_test = nullptr;
    std::size_t selected_test_count = 0;

    try {
        for (const cubey::tests::TestCase& test : cubey::tests::core_tests()) {
            if (!filters.empty() && !matches_any_filter(test.name, filters)) {
                continue;
            }
            active_test = test.name;
            ++selected_test_count;
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

    if (selected_test_count == 0U) {
        std::fprintf(stderr, "cubey_core_tests: no tests matched the provided filters\n");
        return 2;
    }

    return 0;
}
