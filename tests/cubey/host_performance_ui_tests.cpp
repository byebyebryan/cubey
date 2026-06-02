#include <cubey/host/performance_ui.h>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_process_resource_stats_sampler_reports_memory() {
    cubey::host::ProcessResourceStatsSampler sampler;
    const cubey::host::ProcessResourceStats stats = sampler.sample();

    require(stats.memory_available, "process memory stats should be available on Linux dev hosts");
    require(stats.resident_bytes > 0, "process RSS should be non-zero");
    require(stats.virtual_bytes >= stats.resident_bytes,
            "process virtual memory should be at least resident memory");
}

void test_process_resource_stats_sampler_reports_cpu_after_second_sample() {
    cubey::host::ProcessResourceStatsSampler sampler;
    static_cast<void>(sampler.sample());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const cubey::host::ProcessResourceStats stats = sampler.sample();

    require(stats.cpu_available, "process CPU percentage should be available after two samples");
    require(stats.cpu_percent >= 0.0, "process CPU percentage should not be negative");
}
