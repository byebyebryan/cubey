#pragma once

#include <cubey/host/frame_stats.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace cubey::host {

struct ProcessResourceStats {
    bool cpu_available = false;
    double cpu_percent = 0.0;
    bool memory_available = false;
    std::uint64_t resident_bytes = 0;
    std::uint64_t virtual_bytes = 0;
};

class ProcessResourceStatsSampler {
  public:
    [[nodiscard]] ProcessResourceStats sample();

  private:
    bool has_previous_sample_ = false;
    std::chrono::steady_clock::time_point previous_wall_time_{};
    double previous_cpu_seconds_ = 0.0;
};

struct PerformanceCounter {
    const char* label = nullptr;
    std::uint64_t value = 0;
    const char* suffix = nullptr;
};

struct PerformanceUiConfig {
    const char* label = "Performance";
    bool default_open = true;
    std::uint32_t level = 0;
    const char* help = "Shared frame, process, GPU-memory, workload, and GPU-timing statistics.";
    bool gpu_timings_default_open = false;
};

struct PerformanceUiContext {
    std::optional<FrameStatsSnapshot> frame_stats{};
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
    ProcessResourceStats process{};
    std::optional<cubey::vulkan::DeviceMemoryBudgetInfo> device_memory_budget{};
    VkDeviceSize owned_gpu_bytes = 0;
    const char* owned_gpu_label = nullptr;
    std::span<const PerformanceCounter> counters{};
    std::span<const cubey::vulkan::GpuPassTiming> gpu_timings{};
    PerformanceUiConfig config{};
};

void draw_performance_ui(const PerformanceUiContext& ui);

} // namespace cubey::host
