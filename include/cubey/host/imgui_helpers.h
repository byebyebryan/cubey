#pragma once

#include <cubey/host/frame_stats.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace cubey::host {

class ScopedImGuiId {
  public:
    explicit ScopedImGuiId(const char* label) {
        ImGui::PushID(label);
    }
    ~ScopedImGuiId() {
        ImGui::PopID();
    }

    ScopedImGuiId(const ScopedImGuiId&) = delete;
    ScopedImGuiId& operator=(const ScopedImGuiId&) = delete;
};

struct ImGuiControlPanelConfig {
    ImVec2 position{16.0F, 16.0F};
    float width = 430.0F;
    ImGuiCond condition = ImGuiCond_FirstUseEver;
};

[[nodiscard]] inline bool begin_control_panel(const char* title,
                                              ImGuiControlPanelConfig config = {}) {
    ImGui::SetNextWindowPos(config.position, config.condition);
    ImGui::SetNextWindowSize(ImVec2(config.width, 0.0F), config.condition);
    return ImGui::Begin(title);
}

[[nodiscard]] inline bool imgui_section(const char* label, bool default_open) {
    const ImGuiTreeNodeFlags flags =
        default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
    return ImGui::CollapsingHeader(label, flags);
}

template <typename Value, typename NameFn>
bool imgui_enum_combo(const char* label, Value& value, std::span<const Value> values,
                      NameFn&& name_fn) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name_fn(value))) {
        for (Value candidate : values) {
            const bool selected = candidate == value;
            if (ImGui::Selectable(name_fn(candidate), selected)) {
                value = candidate;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

template <typename Value, std::size_t Count, typename NameFn>
bool imgui_enum_combo(const char* label, Value& value, const std::array<Value, Count>& values,
                      NameFn&& name_fn) {
    return imgui_enum_combo(label, value, std::span<const Value>(values), name_fn);
}

[[nodiscard]] inline double bytes_to_mib(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

inline void draw_gpu_timings(std::span<const cubey::vulkan::GpuPassTiming> timings,
                             const char* label = "GPU timings") {
    if (timings.empty()) {
        return;
    }
    ImGui::SeparatorText(label);
    for (const cubey::vulkan::GpuPassTiming& timing : timings) {
        ImGui::Text("%s: %.3f ms", timing.label.c_str(), timing.milliseconds);
    }
}

inline void draw_frame_stats(const std::optional<FrameStatsSnapshot>& stats, double latest_fps,
                             double latest_frame_ms) {
    if (stats.has_value()) {
        ImGui::Text("Frame: %.1f fps / %.2f ms avg (%.2f ms last)", stats->fps, stats->frame_ms,
                    latest_frame_ms);
    } else if (latest_fps > 0.0) {
        ImGui::Text("Frame: %.1f fps / %.2f ms", latest_fps, latest_frame_ms);
    } else {
        ImGui::TextUnformatted("Frame: collecting...");
    }
}

inline void draw_device_memory_budget(VkDeviceSize owned_bytes,
                                      const cubey::vulkan::DeviceMemoryBudgetInfo& memory_budget,
                                      const char* owned_label) {
    ImGui::Text("%s: %.1f MiB", owned_label, bytes_to_mib(owned_bytes));
    if (memory_budget.available && memory_budget.device_local_budget > 0) {
        ImGui::Text("VRAM: %.0f / %.0f MiB used", bytes_to_mib(memory_budget.device_local_usage),
                    bytes_to_mib(memory_budget.device_local_budget));
    } else {
        ImGui::Text("VRAM heap: %.0f MiB (usage unavailable)",
                    bytes_to_mib(memory_budget.device_local_heap_size));
    }
}

} // namespace cubey::host
