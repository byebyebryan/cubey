#include <cubey/host/performance_ui.h>

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>
#include <fstream>
#include <string_view>
#include <sys/resource.h>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace cubey::host {
namespace {

[[nodiscard]] double process_cpu_seconds() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const double user_seconds = static_cast<double>(usage.ru_utime.tv_sec) +
                                static_cast<double>(usage.ru_utime.tv_usec) / 1'000'000.0;
    const double system_seconds = static_cast<double>(usage.ru_stime.tv_sec) +
                                  static_cast<double>(usage.ru_stime.tv_usec) / 1'000'000.0;
    return user_seconds + system_seconds;
}

void sample_process_memory(ProcessResourceStats& stats) {
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::uint64_t virtual_pages = 0;
    std::uint64_t resident_pages = 0;
    if (!(statm >> virtual_pages >> resident_pages)) {
        return;
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return;
    }

    stats.memory_available = true;
    stats.virtual_bytes = virtual_pages * static_cast<std::uint64_t>(page_size);
    stats.resident_bytes = resident_pages * static_cast<std::uint64_t>(page_size);
#else
    (void)stats;
#endif
}

void draw_frame_performance(const PerformanceUiContext& ui) {
    draw_frame_stats(ui.frame_stats, ui.latest_fps, ui.latest_frame_ms);
    if (!ui.frame_stats.has_value()) {
        return;
    }

    const FrameStatsSnapshot& stats = *ui.frame_stats;
    ImGui::Text("Resolution: %u x %u / %.2f Mpix/s", stats.width, stats.height,
                stats.megapixels_per_second);
    ImGui::Text("Triangles: %u", stats.triangles);
}

void draw_process_resources(const ProcessResourceStats& process) {
    if (process.cpu_available) {
        ImGui::Text("CPU: %.1f%% process", process.cpu_percent);
    } else {
        ImGui::TextUnformatted("CPU: collecting...");
    }

    if (process.memory_available) {
        ImGui::Text("RAM: %.1f MiB RSS / %.1f MiB virtual", bytes_to_mib(process.resident_bytes),
                    bytes_to_mib(process.virtual_bytes));
    } else {
        ImGui::TextUnformatted("RAM: unavailable");
    }
}

void draw_gpu_memory(const PerformanceUiContext& ui) {
    if (ui.owned_gpu_label != nullptr && std::string_view(ui.owned_gpu_label).empty() == false) {
        ImGui::Text("%s: %.1f MiB", ui.owned_gpu_label, bytes_to_mib(ui.owned_gpu_bytes));
    }

    if (!ui.device_memory_budget.has_value()) {
        return;
    }

    const cubey::vulkan::DeviceMemoryBudgetInfo& memory = *ui.device_memory_budget;
    if (memory.available && memory.device_local_budget > 0) {
        ImGui::Text("VRAM: %.0f / %.0f MiB used", bytes_to_mib(memory.device_local_usage),
                    bytes_to_mib(memory.device_local_budget));
    } else {
        ImGui::Text("VRAM heap: %.0f MiB (usage unavailable)",
                    bytes_to_mib(memory.device_local_heap_size));
    }
}

void draw_workload_counters(std::span<const PerformanceCounter> counters) {
    if (counters.empty()) {
        return;
    }

    ImGui::SeparatorText("Workload");
    for (const PerformanceCounter& counter : counters) {
        if (counter.label == nullptr || std::string_view(counter.label).empty()) {
            continue;
        }
        if (counter.suffix != nullptr && std::string_view(counter.suffix).empty() == false) {
            ImGui::Text("%s: %llu %s", counter.label,
                        static_cast<unsigned long long>(counter.value), counter.suffix);
        } else {
            ImGui::Text("%s: %llu", counter.label, static_cast<unsigned long long>(counter.value));
        }
    }
}

void draw_performance_gpu_timings(const PerformanceUiContext& ui) {
    if (ui.gpu_timings.empty()) {
        return;
    }

    if (const ScopedImGuiGroup group{"GPU timings",
                                     {.default_open = ui.config.gpu_timings_default_open,
                                      .level = 1U,
                                      .help = "Timestamp query timings for recorded GPU passes."}};
        group) {
        const ScopedImGuiId section_id("GPU timings");
        for (const cubey::vulkan::GpuPassTiming& timing : ui.gpu_timings) {
            ImGui::Text("%s: %.3f ms", timing.label.c_str(), timing.milliseconds);
        }
    }
}

} // namespace

ProcessResourceStats ProcessResourceStatsSampler::sample() {
    ProcessResourceStats stats{};
    sample_process_memory(stats);

    const auto wall_time = std::chrono::steady_clock::now();
    const double cpu_seconds = process_cpu_seconds();

    if (has_previous_sample_) {
        const double wall_seconds =
            std::chrono::duration<double>(wall_time - previous_wall_time_).count();
        const double cpu_delta = std::max(0.0, cpu_seconds - previous_cpu_seconds_);
        if (wall_seconds > 0.0) {
            stats.cpu_available = true;
            stats.cpu_percent = (cpu_delta / wall_seconds) * 100.0;
        }
    }

    previous_wall_time_ = wall_time;
    previous_cpu_seconds_ = cpu_seconds;
    has_previous_sample_ = true;
    return stats;
}

void draw_performance_ui(const PerformanceUiContext& ui) {
    if (const ScopedImGuiGroup group{ui.config.label,
                                     {.default_open = ui.config.default_open,
                                      .level = ui.config.level,
                                      .help = ui.config.help}};
        group) {
        const ScopedImGuiId section_id(ui.config.label);
        draw_frame_performance(ui);
        draw_process_resources(ui.process);
        draw_gpu_memory(ui);
        draw_workload_counters(ui.counters);
        draw_performance_gpu_timings(ui);
    }
}

} // namespace cubey::host
