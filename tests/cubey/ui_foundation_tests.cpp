#include "source_file_test_helpers.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path source_root() {
    return std::filesystem::path(CUBEY_SOURCE_DIR);
}

std::size_t count_occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

std::string remove_ascii_whitespace(std::string_view text) {
    std::string compact;
    compact.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            continue;
        default:
            compact.push_back(ch);
            break;
        }
    }
    return compact;
}

void require_section_default_collapsed(const char* relative_path, const char* label,
                                       const char* message) {
    const std::string source = cubey::tests::read_source_file(source_root() / relative_path);
    const std::string compact = remove_ascii_whitespace(source);
    const std::string needle =
        remove_ascii_whitespace(std::string{"\""} + label + "\",{.default_open=false");
    cubey::tests::require_contains(compact, needle, message);
}

} // namespace

void test_active_project_ui_uses_shared_common_controls() {
    constexpr std::array active_ui_files{
        "projects/atmosphere/atmosphere_ui.cpp",
        "projects/ocean/ocean_ui.cpp",
        "projects/procedural_terrain_legacy/procedural_terrain_ui.cpp",
        "projects/fluid/sim/smoke_2d/smoke_2d_ui.cpp",
        "projects/fluid/sim/water_2d/water_2d_ui.cpp",
        "projects/fluid/sim/water_3d/water_3d_ui.cpp",
        "projects/fluid/sim/pyro_3d/pyro_3d_app.cpp",
    };

    constexpr std::array raw_common_controls{
        "ImGui::Slider", "ImGui::Checkbox", "ImGui::ColorEdit", "ImGui::Input", "ImGui::Button",
    };

    for (const char* relative_path : active_ui_files) {
        const std::string source = cubey::tests::read_source_file(source_root() / relative_path);
        cubey::tests::require_contains(
            source, "cubey::host::", "active UI panels should use shared UI helpers");
        for (const char* raw_control : raw_common_controls) {
            cubey::tests::require_not_contains(
                source, raw_control,
                "active UI panels should route common controls through shared helpers");
        }
    }
}

void test_active_project_ui_raw_combo_exceptions_are_explicit() {
    const std::string ocean =
        cubey::tests::read_source_file(source_root() / "projects/ocean/ocean_ui.cpp");
    const std::string terrain = cubey::tests::read_source_file(
        source_root() / "projects/procedural_terrain_legacy/procedural_terrain_ui.cpp");

    require(count_occurrences(ocean, "ImGui::BeginCombo") == 1U,
            "active ocean UI should only keep the custom cascade selector raw combo");
    require(count_occurrences(ocean, "ImGui::Selectable") == 2U,
            "active ocean UI raw selectable use should stay limited to five-cascade inspection");
    require(count_occurrences(terrain, "ImGui::BeginCombo") == 1U,
            "terrain UI should only keep the custom grid-preserving preset combo");
    require(count_occurrences(terrain, "ImGui::Selectable") == 1U,
            "terrain UI raw selectable use should stay limited to grid presets");
}

void test_retired_ocean_ui_exceptions_are_removed() {
    const std::string docs =
        cubey::tests::read_source_file(source_root() / "docs/architecture/ocean-rendering.md");

    cubey::tests::require_not_contains(
        docs, "intentionally exempt from current active-panel",
        "removed ocean_ref should not remain a UI cleanup exception");
    cubey::tests::require_not_contains(
        docs, "shared UI/config helper adoption",
        "removed ocean_legacy should not remain a UI cleanup exception");
}

void test_imgui_helper_layer_covers_active_common_controls() {
    const std::string helper =
        cubey::tests::read_source_file(source_root() / "include/cubey/host/imgui_helpers.h");

    cubey::tests::require_contains(helper, "imgui_button",
                                   "shared UI helpers should cover command buttons");
    cubey::tests::require_contains(helper, "imgui_input_int",
                                   "shared UI helpers should cover integer inputs");
    cubey::tests::require_contains(helper, "imgui_uint32_combo",
                                   "shared UI helpers should cover simple uint preset combos");
}

void test_active_project_ui_uses_shared_performance_panel() {
    constexpr std::array active_ui_files{
        "projects/atmosphere/atmosphere_ui.cpp",
        "projects/ocean/ocean_ui.cpp",
        "projects/procedural_terrain_legacy/procedural_terrain_ui.cpp",
        "projects/fluid/sim/smoke_2d/smoke_2d_ui.cpp",
        "projects/fluid/sim/water_2d/water_2d_ui.cpp",
        "projects/fluid/sim/water_3d/water_3d_ui.cpp",
        "projects/fluid/sim/pyro_3d/pyro_3d_app.cpp",
    };

    for (const char* relative_path : active_ui_files) {
        const std::string source = cubey::tests::read_source_file(source_root() / relative_path);
        cubey::tests::require_contains(
            source, "draw_performance_ui",
            "active UI panels should route shared stats through the performance panel");
        cubey::tests::require_not_contains(source, "draw_frame_stats",
                                           "active UI panels should not draw frame stats directly");
        cubey::tests::require_not_contains(source, "draw_gpu_timings",
                                           "active UI panels should not draw GPU timings directly");
    }
}

void test_active_project_ui_starts_low_noise_sections_collapsed() {
    require_section_default_collapsed("projects/atmosphere/atmosphere_ui.cpp", "Sun",
                                      "atmosphere manual sun controls should start collapsed");
    require_section_default_collapsed("projects/atmosphere/atmosphere_ui.cpp", "Medium",
                                      "atmosphere medium controls should start collapsed");
    require_section_default_collapsed("projects/atmosphere/atmosphere_ui.cpp", "Night sky",
                                      "atmosphere night controls should start collapsed");
    require_section_default_collapsed("projects/atmosphere/atmosphere_ui.cpp", "Diagnostics",
                                      "atmosphere diagnostics should start collapsed");

    require_section_default_collapsed("projects/procedural_terrain_legacy/procedural_terrain_ui.cpp",
                                      "Diagnostics", "terrain diagnostics should start collapsed");

    require_section_default_collapsed("projects/fluid/sim/smoke_2d/smoke_2d_ui.cpp", "Injectors",
                                      "smoke injector tuning should start collapsed");
    require_section_default_collapsed("projects/fluid/sim/smoke_2d/smoke_2d_ui.cpp", "Diagnostics",
                                      "smoke diagnostics should start collapsed");

    require_section_default_collapsed("projects/fluid/sim/water_2d/water_2d_ui.cpp",
                                      "Sources and forces",
                                      "water 2D source controls should start collapsed");
    require_section_default_collapsed("projects/fluid/sim/water_2d/water_2d_ui.cpp", "Diagnostics",
                                      "water 2D diagnostics should start collapsed");

    require_section_default_collapsed("projects/fluid/sim/water_3d/water_3d_ui.cpp",
                                      "Sources and forces",
                                      "water 3D source controls should start collapsed");
    require_section_default_collapsed("projects/fluid/sim/water_3d/water_3d_ui.cpp", "Diagnostics",
                                      "water 3D diagnostics should start collapsed");

    require_section_default_collapsed("projects/fluid/sim/pyro_3d/pyro_3d_app.cpp", "Simulation",
                                      "pyro solver controls should start collapsed");
    require_section_default_collapsed("projects/fluid/sim/pyro_3d/pyro_3d_app.cpp", "Rendering",
                                      "pyro render tuning should start collapsed");
    require_section_default_collapsed("projects/fluid/sim/pyro_3d/pyro_3d_app.cpp", "Diagnostics",
                                      "pyro diagnostics should start collapsed");

    const std::string pyro = remove_ascii_whitespace(cubey::tests::read_source_file(
        source_root() / "projects/fluid/sim/pyro_3d/pyro_3d_app.cpp"));
    cubey::tests::require_contains(pyro, "model_section,{.default_open=false",
                                   "pyro mode-specific model controls should start collapsed");
}
