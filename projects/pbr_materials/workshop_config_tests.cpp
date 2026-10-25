#include "../sdf/sdf_config.h"
#include "pbr_materials_config.h"
#include "pbr_materials_presentation.h"
#include <stdexcept>
#include <string>
#include <vector>
using namespace cubey::projects;
void require(bool value) {
    if (!value)
        throw std::runtime_error("workshop CLI contract failed");
}
template <class F> auto parse(F fn, std::initializer_list<const char*> values) {
    std::vector<std::string> strings;
    strings.emplace_back("workshop");
    for (auto v : values)
        strings.emplace_back(v);
    std::vector<char*> args;
    for (auto& s : strings)
        args.push_back(s.data());
    return fn(static_cast<int>(args.size()), args.data(), nullptr);
}
int main() {
    const auto mesh =
        parse(pbr_materials::parse_pbr_materials_config,
              {"--material", "marble", "--no-pattern", "--no-bump", "--lighting", "clear"});
    require(!mesh.pattern && !mesh.bump && mesh.environment == "clear");
    const auto sdf_defaults = parse(sdf::parse_sdf_config, {"--workshop", "sphere"});
    require(sdf_defaults.yaw == mesh.yaw && sdf_defaults.pitch == mesh.pitch &&
            sdf_defaults.distance == mesh.distance);
    require(sdf_defaults.exposure == mesh.pbr.exposure &&
            sdf_defaults.lighting == "material-study" && sdf_defaults.glass_crossings == 8);
    const auto explicit_values =
        parse(sdf::parse_sdf_config, {"--workshop", "csg", "--exposure", "0", "--glass-crossings",
                                      "16", "--no-workshop-blend-materials"});
    require(explicit_values.exposure == 0 && explicit_values.glass_crossings == 16 &&
            !explicit_values.workshop_blend_materials);
    const auto legacy = parse(sdf::parse_sdf_config, {});
    require(legacy.workshop.empty() && legacy.exposure == 0 && legacy.lighting == "atmosphere" &&
            legacy.glass_crossings == 4);
    const auto presentation =
        parse(pbr_materials::parse_pbr_materials_config, {"--presentation", "contrast"});
    require(presentation.distance == 2 && presentation.presentation == "contrast");
    const auto explicit_distance = parse(pbr_materials::parse_pbr_materials_config,
                                         {"--presentation", "contrast", "--distance", "2.2"});
    require(explicit_distance.distance == 2.2F && mesh.presentation.empty());
    require(pbr_materials::presentation_yaw(0) == pbr_materials::presentation_yaw(1.5));
    require(pbr_materials::presentation_yaw(6.5) == pbr_materials::presentation_yaw(8));
    float previous = 0;
    for (int frame = 0; frame <= 480; ++frame) {
        const auto yaw = pbr_materials::presentation_yaw(frame / 60.0);
        require(std::isfinite(yaw) && yaw >= previous && yaw <= glm::radians(20.01F));
        previous = yaw;
    }
    const auto layout = pbr_materials::presentation_layout("contrast");
    require(layout[0].shape == "cut-block" && layout[2].shape == "swatch" &&
            layout[3].material == "bronze");
    require(pbr_materials::specimen_thickness(layout[0].shape) == .32F &&
            pbr_materials::specimen_thickness(layout[2].shape) == .036F);
    const auto library = parse(pbr_materials::parse_pbr_materials_config,
                               {"--presentation", "library", "--lighting-cycle", "dawn"});
    require(library.distance == .855F && library.pitch == 0 && library.yaw == 0 &&
            library.lighting_cycle == "dawn");
    const auto all = pbr_materials::presentation_layout("library");
    require(all.size() == 12);
    for (const auto& item : all)
        require(item.shape == "sphere");
    for (const auto* phase : {"dusk", "dawn"})
        for (int frame = 0; frame <= 240; ++frame) {
            const auto mesh_light = pbr_materials::presentation_lighting(phase, frame / 30.0);
            const auto sdf_light = sdf::material_lighting_cycle(phase, frame / 30.0, 8);
            require(mesh_light.sun_elevation == sdf_light.sun_elevation &&
                    mesh_light.sun_azimuth == sdf_light.sun_azimuth &&
                    mesh_light.cloud_seconds == sdf_light.cloud_seconds &&
                    mesh_light.exposure == sdf_light.exposure);
        }
    require(pbr_materials::presentation_lighting("dusk", -1).sun_elevation == 14 &&
            pbr_materials::presentation_lighting("dusk", 9).sun_elevation == -4);
    require(pbr_materials::presentation_lighting("dawn", 0).sun_elevation == -4 &&
            pbr_materials::presentation_lighting("dawn", 8).sun_elevation == 14);
    bool invalid = false;
    try {
        (void)cubey::sample_environment_transition({.duration_seconds = 0}, 1);
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    require(invalid);
}
