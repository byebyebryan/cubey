#pragma once

#include "test_registry.h"

namespace cubey::tests {

[[nodiscard]] std::span<const TestCase> core_test_cases();
[[nodiscard]] std::span<const TestCase> asset_test_cases();
[[nodiscard]] std::span<const TestCase> engine_host_input_test_cases();
[[nodiscard]] std::span<const TestCase> render_test_cases();
[[nodiscard]] std::span<const TestCase> scene_test_cases();
[[nodiscard]] std::span<const TestCase> vulkan_test_cases();

} // namespace cubey::tests
