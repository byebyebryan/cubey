#pragma once
#include <cmath>
#include <cubey/host/configured_app.h>

namespace cubey::projects::raytracing {
struct Config {
    host::CommonRunConfig common;
    std::string scene = "fixture", view = "final";
    std::filesystem::path model, metrics, save_config, query_output;
    float fov = 45.F;
    float yaw = .6F, pitch = .25F, distance = 2.6F, target_y = 0;
    float red = .55F, green = .34F, blue = .15F, roughness = .4F;
    float sun_azimuth = -.8F, sun_elevation = .8F, intensity = 3, ambient = .12F, exposure = 0;
    bool shadows = true;
    std::string fault = "none";
    bool exercise_retry = false;
};
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
    scene.enum_values = {"triangle", "fixture", "dragon"};
    auto view = opt("view", ValueType::Enum);
    view.enum_values = {"final", "geometric-normal", "shading-normal",
                        "depth", "instance-id",      "barycentric"};
    auto fault = opt("fault", ValueType::Enum);
    fault.enum_values = {"none", "scene", "pipeline", "target"};
    b.bind(scene, c.scene).bind(view, c.view).bind(fault, c.fault);
    b.bind(opt("model", ValueType::Path), c.model)
        .bind(opt("metrics", ValueType::Path), c.metrics)
        .bind(opt("save-config", ValueType::Path), c.save_config)
        .bind(opt("query-output", ValueType::Path), c.query_output);
    for (auto [name, value] : {std::pair{"yaw", &c.yaw},
                               {"pitch", &c.pitch},
                               {"distance", &c.distance},
                               {"fov", &c.fov},
                               {"target-y", &c.target_y},
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
    return b.build();
}
inline Config parse_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    config::ParseResult local_result;
    auto* parsed = result ? result : &local_result;
    auto c = host::parse_configured_app<Config>(argc, argv, schema, parsed);
    if (parsed->write_config_template_path)
        return c;
    for (float value :
         {c.fov, c.yaw, c.pitch, c.distance, c.target_y, c.red, c.green, c.blue, c.roughness,
          c.sun_azimuth, c.sun_elevation, c.intensity, c.ambient, c.exposure})
        if (!std::isfinite(value))
            throw std::invalid_argument("raytracing settings must be finite");
    if (!(c.fov >= 10 && c.fov <= 100 && c.distance > .05F && c.distance < 100 &&
          std::abs(c.pitch) < 1.5F && c.roughness >= .04F && c.roughness <= 1 && c.red >= 0 &&
          c.red <= 1 && c.green >= 0 && c.green <= 1 && c.blue >= 0 && c.blue <= 1 &&
          c.intensity >= 0 && c.intensity <= 100 && c.ambient >= 0 && c.ambient <= 10 &&
          std::abs(c.exposure) <= 16))
        throw std::invalid_argument("raytracing: invalid camera, material or lighting range");
    if (c.scene == "dragon" && (c.model.empty() || !std::filesystem::is_regular_file(c.model)))
        throw std::invalid_argument("dragon requires --model prepared.glb; run "
                                    "projects/raytracing/tools/prepare_dragon.py first");
    if (c.scene != "dragon" && !c.model.empty())
        throw std::invalid_argument("--model requires --scene dragon");
    if (c.exercise_retry && c.fault == "none")
        throw std::invalid_argument("--exercise-retry requires --fault scene|pipeline|target");
    if (!c.query_output.empty() &&
        (!c.common.headless || c.common.frames > 1 || c.common.headless_render_only ||
         c.common.capture_mode != CaptureMode::Png || c.common.width > 2048 ||
         c.common.height > 2048 ||
         std::uint64_t(c.common.width) * c.common.height * 64 > 32 * 1024 * 1024))
        throw std::invalid_argument(
            "query output requires one headless PNG frame and at most 32 MiB of raw geometry");
    return c;
}
int run(const Config& config);
} // namespace cubey::projects::raytracing
