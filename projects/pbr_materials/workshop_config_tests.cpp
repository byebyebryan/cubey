#include "../raytracing/raytracing_presentation.h"
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
    require(mesh.sphere_density == "standard");
    require(mesh.transmission_source == "screen");
    require(mesh.common.anti_aliasing == "auto" && !mesh.taa_moving_fixture);
    const auto temporal =
        parse(pbr_materials::parse_pbr_materials_config, {"--taa-moving-fixture"});
    require(temporal.common.anti_aliasing == "taa" && temporal.material == "clay" &&
            temporal.environment == "studio" && temporal.distance == 2.2F && temporal.yaw == 0 &&
            temporal.pitch == 0);
    const auto temporal_off =
        parse(pbr_materials::parse_pbr_materials_config,
              {"--taa-moving-fixture", "--anti-aliasing", "off", "--taa-fixture-time", "3.5"});
    require(temporal_off.common.anti_aliasing == "off" && temporal_off.taa_fixture_time == 3.5);
    bool invalid_aa = false;
    try {
        (void)parse(pbr_materials::parse_pbr_materials_config, {"--anti-aliasing", "fxaa"});
    } catch (const std::runtime_error&) {
        invalid_aa = true;
    }
    require(invalid_aa);
    const auto environment_source =
        parse(pbr_materials::parse_pbr_materials_config, {"--transmission-source", "environment"});
    require(environment_source.transmission_source == "environment");
    auto source_copy = pbr_materials::PbrMaterialsConfig{};
    auto source_document = environment_source;
    const auto document =
        pbr_materials::pbr_materials_config_schema(source_document).template_json();
    pbr_materials::pbr_materials_config_schema(source_copy).apply_json(document);
    require(source_copy.transmission_source == "environment");
    bool invalid_source = false;
    try {
        (void)parse(pbr_materials::parse_pbr_materials_config,
                    {"--transmission-source", "automatic"});
    } catch (const std::runtime_error&) {
        invalid_source = true;
    }
    require(invalid_source);
    require(parse(pbr_materials::parse_pbr_materials_config, {}).distance == 3.3F);
    require(parse(pbr_materials::parse_pbr_materials_config,
                  {"--material", "gallery", "--distance", "1.7"})
                .distance == 1.7F);
    const auto dense =
        parse(pbr_materials::parse_pbr_materials_config, {"--sphere-density", "dense"});
    require(dense.sphere_density == "dense");
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
            raytracing::Config rt;
            rt.presentation = "library";
            rt.lighting_cycle = phase;
            rt.orbit_degrees = 20;
            rt.yaw = 0;
            const auto sampled = raytracing::sample_workshop_presentation(rt, frame / 30.0);
            require(std::abs(glm::degrees(sampled.sun_elevation) - mesh_light.sun_elevation) <
                        2e-6F &&
                    std::abs(glm::degrees(glm::pi<float>() - sampled.sun_azimuth) -
                             mesh_light.sun_azimuth) < 3e-5F &&
                    sampled.exposure == mesh_light.exposure &&
                    std::abs(sampled.yaw - glm::radians(20.F) * static_cast<float>(frame / 240.0)) <
                        1e-7F);
        }
    require(pbr_materials::presentation_lighting("dusk", -1).sun_elevation == 14 &&
            pbr_materials::presentation_lighting("dusk", 9).sun_elevation == -1.2F);
    require(pbr_materials::presentation_lighting("dawn", 0).sun_elevation == -1.2F &&
            pbr_materials::presentation_lighting("dawn", 8).sun_elevation == 14);
    bool invalid = false;
    try {
        (void)cubey::sample_environment_transition({.duration_seconds = 0}, 1);
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    require(invalid);
}
