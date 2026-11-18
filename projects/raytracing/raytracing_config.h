#pragma once
#include <cmath>
#include <cubey/host/configured_app.h>
#include <string_view>

namespace cubey::projects::raytracing {
struct Config {
    host::CommonRunConfig common;
    std::string scene = "fixture", view = "final";
    std::string renderer = "rt", transport_mode = "sampled";
    std::string anti_aliasing = "off";
    std::string forward_transmission_source = "screen";
    std::uint32_t demo_samples = 4;
    std::uint32_t fixed_opaque_samples = 4;
    bool fixed_opaque_sequence = false;
    float forward_thickness = .2F;
    std::filesystem::path model, metrics, save_config, query_output;
    std::filesystem::path optics_output, hdr_output, scene_output;
    std::string environment = "legacy", reflection_source = "scene", transmission_source = "scene";
    std::string subject_material = "mirror";
    std::string support_material = "matte";
    std::string dragon_support = "plinth";
    float fov = 45.F;
    float yaw = .6F, pitch = .25F, distance = 2.6F, target_y = 0, target_x = 0, target_z = 0;
    float red = .55F, green = .34F, blue = .15F, roughness = .4F;
    float sun_azimuth = -.8F, sun_elevation = .8F, intensity = 3, ambient = .12F, exposure = 0;
    bool shadows = true;
    bool controls = true;
    bool hide_dragon = false;
    bool smooth_normals = true;
    std::string fault = "none";
    bool exercise_retry = false;
    bool optics_contract = false;
    bool bsdf_contract = false;
    bool material_contract = false;
    bool dielectric_contract = false;
    std::string material = "bronze", specimen = "sphere";
    std::string sphere_density = "standard";
    std::string optical_pair = "none";
    std::string presentation = "none", lighting_cycle = "frozen";
    double presentation_time = -1;
    float orbit_degrees = 0;
    std::string workshop_lighting = "combined";
    std::filesystem::path recipe_input, recipe_output;
    std::uint32_t samples = 256, seed = 7;
    std::uint32_t stress_edits = 0;
    float material_roughness = -1;
    float object_scale = 1;
    bool pattern = true, bump = true;
    float ior = 1.5F, attenuation_red = .94F, attenuation_green = .98F, attenuation_blue = .99F;
    float attenuation_distance = 1.F, throughput_cutoff = .0001F;
    float firefly_clamp = 0;
    float orbit_speed = 0, sun_speed = 0;
    int max_events = 8, max_path_rays = 32, max_total_rays = 128;
#ifdef CUBEY_RT_RAY_JOURNAL
    std::filesystem::path ray_journal;
    std::uint32_t journal_x = 0, journal_y = 0, journal_sample = 0;
    bool journal_isolate = false, journal_first_failure = false;
#endif
};
inline bool sampled_material_scene(const Config& c) {
    return c.scene == "materials" || c.scene == "dragon-materials" || c.scene == "dragon-optics";
}
inline bool sampled_dragon_scene(const Config& c) {
    return c.scene == "dragon-materials" || c.scene == "dragon-optics";
}
inline bool demo_transport(const Config& c) {
    return c.renderer == "rt" && c.transport_mode == "demo";
}
inline bool fixed_opaque_transport(const Config& c) {
    return c.renderer == "rt" && c.transport_mode == "fixed-opaque";
}
inline bool per_frame_fixed_transport(const Config& c) {
    return demo_transport(c) || fixed_opaque_transport(c);
}
inline std::uint32_t frame_sample_target(const Config& c) {
    return demo_transport(c) ? c.demo_samples
           : fixed_opaque_transport(c) ? c.fixed_opaque_samples
                                       : c.samples;
}
inline std::uint64_t fixed_opaque_sequence_cut_frame(std::uint64_t frame_count) {
    return frame_count >= 96 ? 72 : 11;
}
inline std::string_view fixed_opaque_sequence_phase(std::uint64_t frame,
                                                    std::uint64_t frame_count) {
    const bool long_sequence = frame_count >= 96;
    const std::uint64_t hold_end = long_sequence ? 11 : 2;
    const std::uint64_t orbit_end = long_sequence ? 47 : 7;
    const std::uint64_t stop_end = long_sequence ? 71 : 10;
    if (frame <= hold_end)
        return "hold";
    if (frame <= orbit_end)
        return "orbit";
    if (frame <= stop_end)
        return "stop";
    if (frame == fixed_opaque_sequence_cut_frame(frame_count))
        return "cut";
    return "post-cut-hold";
}
inline float fixed_opaque_sequence_yaw_offset(std::uint64_t frame,
                                              std::uint64_t frame_count) {
    const bool long_sequence = frame_count >= 96;
    const std::uint64_t hold_end = long_sequence ? 11 : 2;
    const std::uint64_t orbit_end = long_sequence ? 47 : 7;
    if (frame <= hold_end)
        return 0.F;
    if (frame <= orbit_end)
        return .16F * static_cast<float>(frame - hold_end) /
               static_cast<float>(orbit_end - hold_end);
    if (frame < fixed_opaque_sequence_cut_frame(frame_count))
        return .16F;
    return -.16F;
}
inline int dump_mode(const Config& c) {
    return !c.query_output.empty()
               ? 1
               : (!c.optics_output.empty() ? 2 : (!c.hdr_output.empty() ? 3 : 0));
}
inline std::uint64_t dump_stride(const Config& c) {
    return dump_mode(c) == 2 ? 256 : (dump_mode(c) == 3 ? 16 : 64);
}
inline config::Schema schema(Config& c) {
    using config::ValueType;
    auto b = config::Schema::builder();
    b.compose(host::common_run_config_schema(c.common));
    auto opt = [](const char* name, ValueType type) {
        return config::OptionSpec{.path = name,
                                  .cli_name = "--" + std::string(name),
                                  .negative_cli_name = {},
                                  .label = name,
                                  .group_path = "Ray tracing",
                                  .help = "Ray-tracing project setting",
                                  .type = type,
                                  .range = {},
                                  .enum_values = {}};
    };
    auto scene = opt("scene", ValueType::Enum);
    scene.enum_values = {"triangle",           "fixture",           "dragon",
                         "reflection-fixture", "reflection-dragon", "reflection-dragon-solo",
                         "refraction-fixture", "refraction-dragon", "slab",
                         "materials",          "dragon-materials",  "dragon-optics"};
    auto view = opt("view", ValueType::Enum);
    view.enum_values = {"final",
                        "geometric-normal",
                        "shading-normal",
                        "depth",
                        "instance-id",
                        "barycentric",
                        "secondary-id",
                        "path-class",
                        "path-events",
                        "transport-limit",
                        "environment",
                        "base-color",
                        "roughness",
                        "metallic",
                        "footprint",
                        "diffuse",
                        "specular",
                        "sheen",
                        "transmission-ray",
                        "transmission-exit",
                        "transmission-radiance"};
    auto fault = opt("fault", ValueType::Enum);
    fault.enum_values = {"none", "scene", "pipeline", "target"};
    b.bind(scene, c.scene).bind(view, c.view).bind(fault, c.fault);
    auto renderer = opt("renderer", ValueType::Enum);
    renderer.enum_values = {"rt", "forward"};
    auto transport = opt("transport-mode", ValueType::Enum);
    transport.enum_values = {"sampled", "demo", "fixed-opaque"};
    auto anti_aliasing = opt("anti-aliasing", ValueType::Enum);
    anti_aliasing.enum_values = {"off", "taa"};
    auto controls = opt("controls", ValueType::Bool);
    controls.negative_cli_name = "--no-controls";
    b.bind(renderer, c.renderer)
        .bind(controls, c.controls)
        .bind(transport, c.transport_mode)
        .bind(anti_aliasing, c.anti_aliasing)
        .bind(opt("demo-samples", ValueType::UInt32), c.demo_samples)
        .bind(opt("fixed-opaque-samples", ValueType::UInt32), c.fixed_opaque_samples)
        .bind(opt("fixed-opaque-sequence", ValueType::Bool), c.fixed_opaque_sequence)
        .bind(opt("forward-thickness", ValueType::Float), c.forward_thickness);
    auto forward_source = opt("forward-transmission-source", ValueType::Enum);
    forward_source.enum_values = {"screen", "environment", "hybrid"};
    b.bind(forward_source, c.forward_transmission_source);
    auto environment = opt("environment", ValueType::Enum);
    environment.enum_values = {"legacy", "atmosphere"};
    auto reflection = opt("reflection-source", ValueType::Enum);
    reflection.enum_values = {"environment", "scene"};
    auto transmission = opt("transmission-source", ValueType::Enum);
    transmission.enum_values = {"environment", "scene"};
    b.bind(environment, c.environment)
        .bind(reflection, c.reflection_source)
        .bind(transmission, c.transmission_source);
    auto subject = opt("subject-material", ValueType::Enum);
    subject.enum_values = {"clay", "mirror"};
    b.bind(subject, c.subject_material);
    auto support_material = opt("support-material", ValueType::Enum);
    support_material.enum_values = {"matte", "marble"};
    b.bind(support_material, c.support_material);
    auto support = opt("dragon-support", ValueType::Enum);
    support.enum_values = {"plinth", "rock", "none"};
    b.bind(support, c.dragon_support);
    b.bind(opt("model", ValueType::Path), c.model)
        .bind(opt("metrics", ValueType::Path), c.metrics)
        .bind(opt("save-config", ValueType::Path), c.save_config)
        .bind(opt("query-output", ValueType::Path), c.query_output)
        .bind(opt("optics-output", ValueType::Path), c.optics_output)
        .bind(opt("hdr-output", ValueType::Path), c.hdr_output)
        .bind(opt("scene-output", ValueType::Path), c.scene_output);
    b.bind(opt("max-events", ValueType::Int), c.max_events)
        .bind(opt("max-path-rays", ValueType::Int), c.max_path_rays)
        .bind(opt("max-total-rays", ValueType::Int), c.max_total_rays);
    auto firefly_clamp = opt("firefly-clamp", ValueType::Float);
    firefly_clamp.help = "Maximum mean RGB of an HDR contribution; zero keeps raw dragon transport";
    b.bind(firefly_clamp, c.firefly_clamp);
    for (auto [name, value] : {std::pair{"yaw", &c.yaw},
                               {"pitch", &c.pitch},
                               {"distance", &c.distance},
                               {"fov", &c.fov},
                               {"target-y", &c.target_y},
                               {"target-x", &c.target_x},
                               {"target-z", &c.target_z},
                               {"ior", &c.ior},
                               {"attenuation-red", &c.attenuation_red},
                               {"attenuation-green", &c.attenuation_green},
                               {"attenuation-blue", &c.attenuation_blue},
                               {"attenuation-distance", &c.attenuation_distance},
                               {"throughput-cutoff", &c.throughput_cutoff},
                               {"orbit-speed", &c.orbit_speed},
                               {"sun-speed", &c.sun_speed},
                               {"red", &c.red},
                               {"green", &c.green},
                               {"blue", &c.blue},
                               {"roughness", &c.roughness},
                               {"sun-azimuth", &c.sun_azimuth},
                               {"sun-elevation", &c.sun_elevation},
                               {"intensity", &c.intensity},
                               {"ambient", &c.ambient},
                               {"exposure", &c.exposure}})
        b.bind(opt(name, ValueType::Float), *value);
    auto shadows = opt("shadows", ValueType::Bool);
    shadows.negative_cli_name = "--no-shadows";
    b.bind(shadows, c.shadows).bind(opt("exercise-retry", ValueType::Bool), c.exercise_retry);
    b.bind(opt("optics-contract", ValueType::Bool), c.optics_contract);
    b.bind(opt("bsdf-contract", ValueType::Bool), c.bsdf_contract);
    b.bind(opt("material-contract", ValueType::Bool), c.material_contract);
    b.bind(opt("dielectric-contract", ValueType::Bool), c.dielectric_contract);
    auto specimen = opt("specimen", ValueType::Enum);
    specimen.enum_values = {"sphere", "block", "swatch", "cut-block"};
    auto workshop_lighting = opt("workshop-lighting", ValueType::Enum);
    auto sphere_density = opt("sphere-density", ValueType::Enum);
    sphere_density.enum_values = {"standard", "dense"};
    auto optical_pair = opt("optical-pair", ValueType::Enum);
    optical_pair.enum_values = {"none", "index-matched", "unequal-ior"};
    auto presentation = opt("presentation", ValueType::Enum);
    presentation.enum_values = {"none", "library"};
    auto cycle = opt("lighting-cycle", ValueType::Enum);
    cycle.enum_values = {"frozen", "dawn", "dusk"};
    workshop_lighting.enum_values = {"combined", "sky", "sun", "furnace"};
    b.bind(opt("material", ValueType::String), c.material)
        .bind(specimen, c.specimen)
        .bind(sphere_density, c.sphere_density)
        .bind(optical_pair, c.optical_pair)
        .bind(presentation, c.presentation)
        .bind(cycle, c.lighting_cycle)
        .bind(opt("presentation-time", ValueType::Float), c.presentation_time)
        .bind(opt("orbit-degrees", ValueType::Float), c.orbit_degrees)
        .bind(workshop_lighting, c.workshop_lighting)
        .bind(opt("recipe-input", ValueType::Path), c.recipe_input)
        .bind(opt("recipe-output", ValueType::Path), c.recipe_output)
        .bind(opt("samples", ValueType::UInt32), c.samples)
        .bind(opt("seed", ValueType::UInt32), c.seed)
        .bind(opt("stress-edits", ValueType::UInt32), c.stress_edits)
        .bind(opt("material-roughness", ValueType::Float), c.material_roughness);
    b.bind(opt("object-scale", ValueType::Float), c.object_scale);
    b.bind(opt("hide-dragon", ValueType::Bool), c.hide_dragon);
    auto smooth_normals = opt("smooth-normals", ValueType::Bool);
    smooth_normals.negative_cli_name = "--no-smooth-normals";
    smooth_normals.help = "Use guarded smooth interfaces for sharp dragon glass";
    b.bind(smooth_normals, c.smooth_normals);
    for (auto [name, value] : {std::pair{"pattern", &c.pattern}, {"bump", &c.bump}}) {
        auto option = opt(name, ValueType::Bool);
        option.negative_cli_name = "--no-" + std::string(name);
        b.bind(option, *value);
    }
#ifdef CUBEY_RT_RAY_JOURNAL
    b.bind(opt("ray-journal", ValueType::Path), c.ray_journal)
        .bind(opt("journal-x", ValueType::UInt32), c.journal_x)
        .bind(opt("journal-y", ValueType::UInt32), c.journal_y)
        .bind(opt("journal-sample", ValueType::UInt32), c.journal_sample)
        .bind(opt("journal-isolate", ValueType::Bool), c.journal_isolate)
        .bind(opt("journal-first-failure", ValueType::Bool), c.journal_first_failure);
#endif
    return b.build();
}
inline Config parse_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    config::ParseResult local_result;
    auto* parsed = result ? result : &local_result;
    auto c = host::parse_configured_app<Config>(argc, argv, schema, parsed);
    if (parsed->write_config_template_path)
        return c;
    if ((c.renderer == "forward" || c.transport_mode == "demo") && c.scene != "dragon-optics")
        throw std::invalid_argument("forward comparison and demo transport require dragon-optics");
    if (c.transport_mode == "fixed-opaque" && c.renderer != "rt")
        throw std::invalid_argument("fixed-opaque transport requires the RT renderer");
    if (fixed_opaque_transport(c) && c.scene != "dragon-materials")
        throw std::invalid_argument("fixed-opaque transport requires dragon-materials");
    if (c.anti_aliasing == "taa" && !fixed_opaque_transport(c))
        throw std::invalid_argument("TAA currently requires fixed-opaque transport");
    if (c.fixed_opaque_sequence && !fixed_opaque_transport(c))
        throw std::invalid_argument("fixed-opaque sequence requires fixed-opaque transport");
    if (c.demo_samples != 2 && c.demo_samples != 4)
        throw std::invalid_argument("demo samples must be 2 or 4 fixed samples per frame");
    if (c.fixed_opaque_samples != 2 && c.fixed_opaque_samples != 4)
        throw std::invalid_argument("fixed-opaque samples must be 2 or 4 primary samples per frame");
    if (!std::isfinite(c.forward_thickness) || c.forward_thickness <= 0 || c.forward_thickness > 2)
        throw std::invalid_argument("forward thickness must be in (0,2]");
    if (c.renderer == "forward" &&
        (dump_mode(c) == 1 || dump_mode(c) == 2 || c.exercise_retry || c.fault != "none"))
        throw std::invalid_argument(
            "forward comparison does not support RT query dumps or retry faults");
    const bool sampled_dragon = sampled_dragon_scene(c);
    if (sampled_dragon) {
        auto fallback = [&](const char* path, auto& value, auto next) {
            if (!parsed->path_was_assigned(path))
                value = next;
        };
        fallback("environment", c.environment, std::string("atmosphere"));
        fallback("dragon-support", c.dragon_support, std::string("rock"));
        fallback("support-material", c.support_material, std::string("marble"));
        fallback("yaw", c.yaw, .45F);
        fallback("pitch", c.pitch, .35F);
        fallback("distance", c.distance, 1.75F);
        fallback("target-y", c.target_y, .32F);
        fallback("sun-azimuth", c.sun_azimuth, .6F);
        fallback("sun-elevation", c.sun_elevation, .38F);
        fallback("throughput-cutoff", c.throughput_cutoff, 0.F);
        if (c.scene == "dragon-optics") {
            fallback("material", c.material, std::string("glass"));
            fallback("firefly-clamp", c.firefly_clamp, 100.F);
            fallback("max-events", c.max_events, 128);
            fallback("max-path-rays", c.max_path_rays, 1024);
            fallback("max-total-rays", c.max_total_rays, 4096);
            if (c.reflection_source != "scene" || c.transmission_source != "scene")
                throw std::invalid_argument(
                    "dragon optics requires full scene reflection and transmission");
        }
        if (c.environment != "atmosphere" || c.dragon_support != "rock" ||
            c.support_material != "marble" || c.optical_pair != "none" || c.object_scale != 1)
            throw std::invalid_argument(
                "sampled dragon requires its marble slab, shared atmosphere and original scale");
        if (c.throughput_cutoff != 0)
            throw std::invalid_argument("sampled dragon requires zero raw-throughput cutoff");
    }
    if (fixed_opaque_transport(c)) {
        if (c.samples != 256 || c.demo_samples != 4 || c.material != "bronze" ||
            c.support_material != "marble" ||
            c.dragon_support != "rock" || c.environment != "atmosphere" ||
            c.reflection_source != "scene" || c.transmission_source != "scene" ||
            c.view != "final" || !c.pattern || !c.bump || !c.shadows ||
            c.workshop_lighting != "combined" || c.material_roughness != -1 ||
            !c.recipe_input.empty() || !c.recipe_output.empty() || c.stress_edits != 0 ||
            c.firefly_clamp != 0 || c.optical_pair != "none" || c.object_scale != 1 ||
            c.sun_speed != 0 ||
            c.hide_dragon || c.fault != "none" || c.exercise_retry || c.optics_contract ||
            c.bsdf_contract || c.material_contract || c.dielectric_contract || dump_mode(c) != 0)
            throw std::invalid_argument(
                "fixed-opaque requires the unmodified bronze dragon and opaque marble support "
                "with final scene transport");
        if (c.fixed_opaque_sequence &&
            (!c.common.headless ||
             !((c.common.headless_render_only && c.common.capture_mode == CaptureMode::Png &&
                c.common.frames == 14) ||
               (!c.common.headless_render_only && c.common.capture_mode == CaptureMode::Video &&
                c.common.frames == 96 && c.common.fps == 30)) ||
             c.orbit_speed != 0 || c.sun_speed != 0))
            throw std::invalid_argument(
                "fixed-opaque sequence requires headless 14-frame render-only or 96-frame 30 Hz "
                "video capture and no animated camera or sun");
    }
    if (c.dragon_support != "plinth" && c.scene != "reflection-dragon-solo" && !sampled_dragon)
        throw std::invalid_argument("dragon support selection requires the solo dragon scene");
    if (c.support_material == "marble" &&
        ((c.scene != "reflection-dragon-solo" && !sampled_dragon) || c.dragon_support != "rock" ||
         c.environment != "atmosphere"))
        throw std::invalid_argument(
            "marble base requires the solo dragon's stone support under shared atmosphere");
    if (c.scene == "materials") {
        if (!std::isfinite(c.object_scale) || c.object_scale < .1F || c.object_scale > 10)
            throw std::invalid_argument("workshop object scale must be in [0.1,10]");
        auto fallback = [&](const char* path, auto& value, auto next) {
            if (!parsed->path_was_assigned(path))
                value = next;
        };
        if (c.optical_pair != "none")
            fallback("material", c.material, std::string("gallery"));
        fallback("environment", c.environment, std::string("atmosphere"));
        fallback("reflection-source", c.reflection_source,
                 std::string(c.optical_pair == "none" ? "environment" : "scene"));
        fallback("transmission-source", c.transmission_source,
                 std::string(c.optical_pair == "none" ? "environment" : "scene"));
        fallback("yaw", c.yaw, 0.F);
        fallback("pitch", c.pitch, c.material == "gallery" ? 0.F : .15F);
        fallback("distance", c.distance, c.material == "gallery" ? .855F : 1.05F);
        fallback("sun-elevation", c.sun_elevation, .13962634016F);
        fallback("sun-azimuth", c.sun_azimuth, -.436332312999F);
        fallback("shadows", c.shadows, false);
        fallback("throughput-cutoff", c.throughput_cutoff, 0.F);
        if (c.environment != "atmosphere")
            throw std::invalid_argument("material workshop requires the shared atmosphere");
        if (c.throughput_cutoff != 0)
            throw std::invalid_argument(
                "sampled workshop transport requires zero raw-throughput cutoff");
    }
    if (c.scene != "materials" && !sampled_dragon &&
        (c.view == "base-color" || c.view == "roughness" || c.view == "metallic" ||
         c.view == "footprint" || c.view == "diffuse" || c.view == "specular" || c.view == "sheen"))
        throw std::invalid_argument("material component views require a sampled material scene");
    if (!std::isfinite(c.material_roughness) ||
        (c.material_roughness != -1 && (c.material_roughness < .05F || c.material_roughness > 1)) ||
        c.samples < 1 || c.samples > 8192 || c.stress_edits > 10000)
        throw std::invalid_argument("invalid workshop sample target or material roughness");
    if (!std::isfinite(c.firefly_clamp) || c.firefly_clamp < 0 || c.firefly_clamp > 1000000)
        throw std::invalid_argument("firefly clamp must be finite and between zero and 1000000");
    if (c.firefly_clamp > 0 && c.scene != "dragon-optics")
        throw std::invalid_argument("firefly clamp currently requires the optical dragon scene");
    const bool forward_view = c.view == "final" || c.view == "shading-normal" ||
                              c.view == "geometric-normal" || c.view == "base-color" ||
                              c.view == "roughness" || c.view == "metallic" ||
                              c.view == "transmission-ray" || c.view == "transmission-exit" ||
                              c.view == "transmission-radiance";
    if (c.renderer != "forward" && c.view.starts_with("transmission-"))
        throw std::invalid_argument("transmission component views require the forward renderer");
    if (c.renderer == "forward" &&
        (c.shadows || !forward_view || c.stress_edits || c.material_contract || c.bsdf_contract ||
         c.optics_contract || c.dielectric_contract || c.workshop_lighting != "combined" ||
         c.intensity != 3 || !c.smooth_normals))
        throw std::invalid_argument("forward comparison requires --no-shadows, a supported view, "
                                    "combined default lighting, smooth normals and no RT probes");
    if (demo_transport(c) &&
        (c.material_contract || c.bsdf_contract || c.optics_contract || c.dielectric_contract))
        throw std::invalid_argument("demo transport does not support sampled contract probes");
    if (c.optical_pair != "none" &&
        (c.scene != "materials" || !c.common.headless || c.material != "gallery"))
        throw std::invalid_argument("optical pair controls require the headless materials gallery");
    if (!std::isfinite(c.presentation_time) ||
        (c.presentation_time < 0 && c.presentation_time != -1) || c.presentation_time > 8 ||
        !std::isfinite(c.orbit_degrees) || std::abs(c.orbit_degrees) > 90)
        throw std::invalid_argument("invalid material presentation time or orbit");
    if (c.presentation == "library") {
        if (c.scene != "materials" || c.material != "gallery" || c.optical_pair != "none" ||
            c.orbit_speed != 0 || c.sun_speed != 0 || c.stress_edits != 0)
            throw std::invalid_argument(
                "library presentation requires an unmodified materials gallery timeline");
    } else if (c.lighting_cycle != "frozen" || c.presentation_time != -1 || c.orbit_degrees != 0)
        throw std::invalid_argument("material timeline controls require --presentation library");
    if (c.scene == "materials" && c.material == "gallery" &&
        (!c.recipe_input.empty() || !c.recipe_output.empty() || c.material_roughness != -1 ||
         c.object_scale != 1))
        throw std::invalid_argument(
            "recipe operations and roughness override require an isolated material");
    for (float value : {c.fov,
                        c.yaw,
                        c.pitch,
                        c.distance,
                        c.target_y,
                        c.target_x,
                        c.target_z,
                        c.red,
                        c.green,
                        c.blue,
                        c.roughness,
                        c.sun_azimuth,
                        c.sun_elevation,
                        c.intensity,
                        c.ambient,
                        c.exposure,
                        c.ior,
                        c.attenuation_red,
                        c.attenuation_green,
                        c.attenuation_blue,
                        c.attenuation_distance,
                        c.throughput_cutoff,
                        c.orbit_speed,
                        c.sun_speed})
        if (!std::isfinite(value))
            throw std::invalid_argument("raytracing settings must be finite");
    if (!(c.fov >= 10 && c.fov <= 100 && c.distance > .05F && c.distance < 100 &&
          std::abs(c.pitch) < 1.5F && c.roughness >= .04F && c.roughness <= 1 && c.red >= 0 &&
          c.red <= 1 && c.green >= 0 && c.green <= 1 && c.blue >= 0 && c.blue <= 1 &&
          c.intensity >= 0 && c.intensity <= 100 && c.ambient >= 0 && c.ambient <= 10 &&
          std::abs(c.exposure) <= 16))
        throw std::invalid_argument("raytracing: invalid camera, material or lighting range");
    if (c.ior < 1 || c.ior > 2.5F || c.attenuation_distance <= 0 || c.attenuation_distance > 100 ||
        c.attenuation_red <= 0 || c.attenuation_red > 1 || c.attenuation_green <= 0 ||
        c.attenuation_green > 1 || c.attenuation_blue <= 0 || c.attenuation_blue > 1 ||
        c.throughput_cutoff < 0 || c.throughput_cutoff > .01F || c.max_events < 1 ||
        c.max_events > (c.scene == "dragon-optics" ? 256 : 12) || c.max_path_rays < 1 ||
        c.max_path_rays > (c.scene == "dragon-optics" ? 2048 : 64) || c.max_total_rays < 1 ||
        c.max_total_rays > (c.scene == "dragon-optics" ? 8192 : 256) ||
        std::abs(c.orbit_speed) > 2 || std::abs(c.sun_speed) > 2)
        throw std::invalid_argument("raytracing: invalid optical range or path budget");
    if (c.hide_dragon && c.scene != "dragon-optics")
        throw std::invalid_argument("hidden dragon control requires the optical dragon scene");
    const bool dragon = c.scene.find("dragon") != std::string::npos;
    if (dragon && (c.model.empty() || !std::filesystem::is_regular_file(c.model)))
        throw std::invalid_argument("dragon requires --model prepared.glb; run "
                                    "projects/raytracing/tools/prepare_dragon.py first");
    if (!dragon && !c.model.empty())
        throw std::invalid_argument("--model requires --scene dragon");
    if (c.exercise_retry && c.fault == "none")
        throw std::invalid_argument("--exercise-retry requires --fault scene|pipeline|target");
    const auto outputs =
        int(!c.query_output.empty()) + int(!c.optics_output.empty()) + int(!c.hdr_output.empty());
    if (int(c.optics_contract) + int(c.bsdf_contract) + int(c.material_contract) +
            int(c.dielectric_contract) >
        1)
        throw std::invalid_argument("select one contract probe");
    if (c.dielectric_contract && (dump_mode(c) != 2 || !c.common.headless))
        throw std::invalid_argument("dielectric contract requires headless --optics-output");
    if (c.optics_contract && (dump_mode(c) != 2 || !c.common.headless))
        throw std::invalid_argument("optics contract requires headless --optics-output");
    if (c.bsdf_contract && (dump_mode(c) != 2 || !c.common.headless || c.optics_contract))
        throw std::invalid_argument(
            "BSDF contract requires headless --optics-output and no optics contract");
    if (c.material_contract && (dump_mode(c) != 2 || !c.common.headless || c.scene != "materials" ||
                                c.bsdf_contract || c.optics_contract))
        throw std::invalid_argument(
            "material contract requires the headless materials scene and --optics-output");
    if (outputs > 1)
        throw std::invalid_argument("select one raw diagnostic output");
    if (outputs && (!c.common.headless || c.common.frames > 1 || c.common.headless_render_only ||
                    c.common.capture_mode != CaptureMode::Png || c.common.width > 2048 ||
                    c.common.height > 2048 ||
                    std::uint64_t(c.common.width) * c.common.height * dump_stride(c) >
                        (dump_mode(c) == 3 ? 64ULL : 32ULL) * 1024 * 1024))
        throw std::invalid_argument(
            "query output requires one headless PNG frame and at most 32 MiB of raw geometry");
#ifdef CUBEY_RT_RAY_JOURNAL
    if (c.ray_journal.empty() || c.scene != "dragon-optics" || !c.common.headless ||
        c.common.frames != 1 || c.common.headless_render_only || c.material != "glass" ||
        c.common.capture_mode != CaptureMode::Png || c.journal_x >= c.common.width ||
        c.journal_y >= c.common.height || c.journal_sample >= c.samples ||
        (c.journal_isolate && (outputs || c.journal_first_failure)))
        throw std::invalid_argument("ray journal requires one headless sharp-glass dragon PNG, "
                                    "a valid pixel/sample and no raw dump in isolated mode");
#endif
    return c;
}
int run(const Config& config);
} // namespace cubey::projects::raytracing
