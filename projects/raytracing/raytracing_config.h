#pragma once
#include <cmath>
#include <cubey/host/configured_app.h>

namespace cubey::projects::raytracing {
struct Config {
    host::CommonRunConfig common;
    std::string scene = "fixture", view = "final";
    std::filesystem::path model, metrics, save_config, query_output;
    std::filesystem::path optics_output, hdr_output, scene_output;
    std::string environment = "legacy", reflection_source = "scene", transmission_source = "scene";
    std::string subject_material = "mirror";
    float fov = 45.F;
    float yaw = .6F, pitch = .25F, distance = 2.6F, target_y = 0, target_x = 0, target_z = 0;
    float red = .55F, green = .34F, blue = .15F, roughness = .4F;
    float sun_azimuth = -.8F, sun_elevation = .8F, intensity = 3, ambient = .12F, exposure = 0;
    bool shadows = true;
    std::string fault = "none";
    bool exercise_retry = false;
    bool optics_contract = false;
    bool bsdf_contract = false;
    bool material_contract = false;
    std::string material = "bronze", specimen = "sphere";
    std::string workshop_lighting = "combined";
    std::filesystem::path recipe_input, recipe_output;
    std::uint32_t samples = 256, seed = 7;
    std::uint32_t stress_edits = 0;
    float material_roughness = -1;
    float object_scale = 1;
    bool pattern = true, bump = true;
    float ior = 1.5F, attenuation_red = .94F, attenuation_green = .98F, attenuation_blue = .99F;
    float attenuation_distance = 1.F, throughput_cutoff = .0001F;
    float orbit_speed = 0, sun_speed = 0;
    int max_events = 8, max_path_rays = 32, max_total_rays = 128;
};
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
                         "materials"};
    auto view = opt("view", ValueType::Enum);
    view.enum_values = {
        "final",       "geometric-normal", "shading-normal", "depth",       "instance-id",
        "barycentric", "secondary-id",     "path-class",     "path-events", "transport-limit",
        "environment", "base-color",       "roughness",      "metallic",    "footprint",
        "diffuse",     "specular",         "sheen"};
    auto fault = opt("fault", ValueType::Enum);
    fault.enum_values = {"none", "scene", "pipeline", "target"};
    b.bind(scene, c.scene).bind(view, c.view).bind(fault, c.fault);
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
    auto specimen = opt("specimen", ValueType::Enum);
    specimen.enum_values = {"sphere", "block", "swatch", "cut-block"};
    auto workshop_lighting = opt("workshop-lighting", ValueType::Enum);
    workshop_lighting.enum_values = {"combined", "sky", "sun", "furnace"};
    b.bind(opt("material", ValueType::String), c.material)
        .bind(specimen, c.specimen)
        .bind(workshop_lighting, c.workshop_lighting)
        .bind(opt("recipe-input", ValueType::Path), c.recipe_input)
        .bind(opt("recipe-output", ValueType::Path), c.recipe_output)
        .bind(opt("samples", ValueType::UInt32), c.samples)
        .bind(opt("seed", ValueType::UInt32), c.seed)
        .bind(opt("stress-edits", ValueType::UInt32), c.stress_edits)
        .bind(opt("material-roughness", ValueType::Float), c.material_roughness);
    b.bind(opt("object-scale", ValueType::Float), c.object_scale);
    for (auto [name, value] : {std::pair{"pattern", &c.pattern}, {"bump", &c.bump}}) {
        auto option = opt(name, ValueType::Bool);
        option.negative_cli_name = "--no-" + std::string(name);
        b.bind(option, *value);
    }
    return b.build();
}
inline Config parse_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    config::ParseResult local_result;
    auto* parsed = result ? result : &local_result;
    auto c = host::parse_configured_app<Config>(argc, argv, schema, parsed);
    if (parsed->write_config_template_path)
        return c;
    if (c.scene == "materials") {
        if (!std::isfinite(c.object_scale) || c.object_scale < .1F || c.object_scale > 10)
            throw std::invalid_argument("workshop object scale must be in [0.1,10]");
        auto fallback = [&](const char* path, auto& value, auto next) {
            if (!parsed->path_was_assigned(path))
                value = next;
        };
        fallback("environment", c.environment, std::string("atmosphere"));
        fallback("reflection-source", c.reflection_source, std::string("environment"));
        fallback("transmission-source", c.transmission_source, std::string("environment"));
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
    if (c.scene != "materials" &&
        (c.view == "base-color" || c.view == "roughness" || c.view == "metallic" ||
         c.view == "footprint" || c.view == "diffuse" || c.view == "specular" || c.view == "sheen"))
        throw std::invalid_argument("material component views require --scene materials");
    if (!std::isfinite(c.material_roughness) ||
        (c.material_roughness != -1 && (c.material_roughness < .05F || c.material_roughness > 1)) ||
        c.samples < 1 || c.samples > 8192 || c.stress_edits > 10000)
        throw std::invalid_argument("invalid workshop sample target or material roughness");
    if (c.scene == "materials" && c.material == "gallery" &&
        (!c.recipe_input.empty() || !c.recipe_output.empty() || c.material_roughness != -1))
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
        c.max_events > 12 || c.max_path_rays < 1 || c.max_path_rays > 64 || c.max_total_rays < 1 ||
        c.max_total_rays > 256 || std::abs(c.orbit_speed) > 2 || std::abs(c.sun_speed) > 2)
        throw std::invalid_argument("raytracing: invalid optical range or path budget");
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
    return c;
}
int run(const Config& config);
} // namespace cubey::projects::raytracing
