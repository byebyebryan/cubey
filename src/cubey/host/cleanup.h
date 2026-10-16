#pragma once

#include <cubey/vulkan/gpu_runtime.h>

#include <exception>
#include <utility>

namespace cubey::host::detail {

// Finish every cleanup action before reporting the first failure. In particular,
// a callback that queues work and then throws must still reach its idle barrier.
template <typename... Actions> void cleanup_all(Actions&&... actions) {
    std::exception_ptr failure;
    const auto attempt = [&failure](auto&& action) {
        try {
            std::forward<decltype(action)>(action)();
        } catch (...) {
            if (failure == nullptr) {
                failure = std::current_exception();
            }
        }
    };
    (attempt(std::forward<Actions>(actions)), ...);
    if (failure != nullptr) {
        std::rethrow_exception(failure);
    }
}

inline void wait_for_gpu(vulkan::GpuRuntime& gpu, const char* label) {
    std::exception_ptr failure;
    // Inline drain deliberately stops at a failed raw callback. Cleanup must
    // also finish its restored tail before waiting for submitted GPU work.
    do {
        try {
            static_cast<void>(gpu.drain());
        } catch (...) {
            if (failure == nullptr) {
                failure = std::current_exception();
            }
        }
    } while (gpu.execution_mode() == vulkan::GpuRuntimeExecutionMode::Inline && !gpu.empty());
    cleanup_all(
        [&] {
            if (failure != nullptr) {
                std::rethrow_exception(failure);
            }
        },
        [&] { gpu.wait_queue_idle(label); });
}

} // namespace cubey::host::detail
