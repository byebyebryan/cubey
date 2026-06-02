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

} // namespace

void test_active_project_ui_uses_shared_common_controls() {
    constexpr std::array active_ui_files{
        "projects/atmosphere/atmosphere_ui.cpp",
        "projects/ocean/ocean_ui.cpp",
        "projects/procedural_terrain/procedural_terrain_ui.cpp",
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
        source_root() / "projects/procedural_terrain/procedural_terrain_ui.cpp");

    require(count_occurrences(ocean, "ImGui::BeginCombo") == 1U,
            "active ocean UI should only keep the custom cascade selector raw combo");
    require(count_occurrences(ocean, "ImGui::Selectable") == 2U,
            "active ocean UI raw selectable use should stay limited to cascade inspection");
    require(count_occurrences(terrain, "ImGui::BeginCombo") == 1U,
            "terrain UI should only keep the custom grid-preserving preset combo");
    require(count_occurrences(terrain, "ImGui::Selectable") == 1U,
            "terrain UI raw selectable use should stay limited to grid presets");
}

void test_reference_ocean_ui_exceptions_are_documented() {
    const std::string docs =
        cubey::tests::read_source_file(source_root() / "docs/architecture/ocean-rendering.md");

    cubey::tests::require_contains(
        docs, "intentionally exempt from current active-panel",
        "ocean_ref should be documented as a frozen UI cleanup exception");
    cubey::tests::require_contains(
        docs, "shared UI/config helper adoption",
        "ocean_legacy should be documented as a donor UI cleanup exception");
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
