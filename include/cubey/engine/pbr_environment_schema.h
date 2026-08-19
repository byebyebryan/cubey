#pragma once

#include <cubey/core/config_schema.h>

#include <cmath>
#include <charconv>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey {

// The startup PBR exposure is shared by the atmosphere, ocean, and glTF
// facades.  Keep the explicit bit next to the value: renderers use it to
// distinguish a manual exposure from automatic exposure.
struct PbrExposureOptions {
    float exposure = 0.0F;
    bool exposure_explicit = false;
};

// Static image-based lighting is shared by the material-cubes example and the
// glTF viewer.  Keep the common path, intensity, rotation, and exposure
// contract together while allowing the viewer to layer its atmosphere source
// selector separately.
struct PbrStaticIblOptions : PbrExposureOptions {
    std::optional<std::filesystem::path> environment_path{};
    float ibl_intensity = 1.0F;
    float environment_rotation_degrees = 0.0F;
};

namespace detail {

inline config::OptionSpec pbr_exposure_option() {
    return {.path = "pbr.exposure",
            .cli_name = "--exposure",
            .negative_cli_name = {},
            .label = "Exposure",
            .group_path = "PBR",
            .help = "Manual exposure bias in stops.",
            .type = config::ValueType::Float,
            .range = {.has_min = true, .has_max = true, .min = -4.0, .max = 4.0},
            .enum_values = {}};
}

[[nodiscard]] inline float parse_pbr_exposure_text(std::string_view value) {
    double parsed = 0.0;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        !std::isfinite(parsed)) {
        throw std::runtime_error("invalid float for pbr.exposure");
    }
    if (parsed < -4.0) {
        throw std::runtime_error("pbr.exposure is below its minimum");
    }
    if (parsed > 4.0) {
        throw std::runtime_error("pbr.exposure is above its maximum");
    }
    return static_cast<float>(parsed);
}

inline void validate_pbr_exposure_json(const nlohmann::json& value) {
    if (value.is_null()) {
        return;
    }
    if (!value.is_number()) {
        throw std::runtime_error("wrong JSON type for config option: pbr.exposure");
    }
    const double parsed = value.get<double>();
    if (!std::isfinite(parsed)) {
        throw std::runtime_error("invalid float for pbr.exposure");
    }
    if (parsed < -4.0) {
        throw std::runtime_error("pbr.exposure is below its minimum");
    }
    if (parsed > 4.0) {
        throw std::runtime_error("pbr.exposure is above its maximum");
    }
}

} // namespace detail

[[nodiscard]] inline config::Schema pbr_exposure_schema(PbrExposureOptions& options) {
    auto builder = config::Schema::builder();
    builder.bind_custom(
        detail::pbr_exposure_option(),
        [&options](std::string_view value) {
            options.exposure = detail::parse_pbr_exposure_text(value);
            options.exposure_explicit = true;
        },
        [&options](const nlohmann::json& value) {
            detail::validate_pbr_exposure_json(value);
            if (!value.is_null()) {
                options.exposure = value.get<float>();
                options.exposure_explicit = true;
            }
        },
        [&options] {
            return options.exposure_explicit ? nlohmann::json(options.exposure)
                                             : nlohmann::json(nullptr);
        });
    return std::move(builder).build();
}

[[nodiscard]] inline config::Schema pbr_static_ibl_schema(PbrStaticIblOptions& options) {
    auto builder = config::Schema::builder();
    builder.bind({.path = "pbr.environment",
                  .cli_name = "--environment",
                  .negative_cli_name = {},
                  .label = "Environment",
                  .group_path = "PBR",
                  .help = "HDR environment path for static IBL.",
                  .type = config::ValueType::Path,
                  .range = {},
                  .enum_values = {}},
                  options.environment_path);
    builder.bind({.path = "pbr.ibl_intensity",
                  .cli_name = "--ibl-intensity",
                  .negative_cli_name = {},
                  .label = "IBL Intensity",
                  .group_path = "PBR",
                  .help = "Multiplier for image-based lighting.",
                  .type = config::ValueType::Float,
                  .range = {.has_min = true, .min = 0.0},
                  .enum_values = {}},
                  options.ibl_intensity);
    builder.bind({.path = "pbr.environment_rotation_degrees",
                  .cli_name = "--environment-rotation-degrees",
                  .negative_cli_name = {},
                  .label = "Environment Rotation",
                  .group_path = "PBR",
                  .help = "Yaw rotation applied to the environment in degrees.",
                  .type = config::ValueType::Float,
                  .range = {},
                  .enum_values = {}},
                  options.environment_rotation_degrees);
    builder.compose(pbr_exposure_schema(options));
    return std::move(builder).build();
}

} // namespace cubey
