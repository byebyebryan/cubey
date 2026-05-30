#include "atmosphere_config.h"

#include <cubey/core/run_config.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Fn> void require_throws(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void require_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    using namespace cubey::projects::atmosphere;

    for (const AtmosphereRenderView view : kAtmosphereRenderViews) {
        require(atmosphere_render_view_from_name(atmosphere_render_view_name(view)) == view,
                "atmosphere render view names should round trip");
    }
    require(next_atmosphere_render_view(AtmosphereRenderView::Final) ==
                AtmosphereRenderView::Rayleigh,
            "atmosphere render views should cycle in debug order");
    require(next_atmosphere_render_view(AtmosphereRenderView::AerialPerspective) ==
                AtmosphereRenderView::Final,
            "atmosphere render view cycle should wrap");
    require_throws([] { static_cast<void>(atmosphere_render_view_from_name("density")); },
                   "atmosphere render view parser should reject unknown views");

    for (const AtmospherePreset preset : kAtmospherePresets) {
        require(atmosphere_preset_from_name(atmosphere_preset_name(preset)) == preset,
                "atmosphere preset names should round trip");
        validate_atmosphere_config(atmosphere_config_for_preset(preset));
    }
    require(atmosphere_config_for_preset(AtmospherePreset::Hazy).mie_density_scale >
                atmosphere_config_for_preset(AtmospherePreset::Noon).mie_density_scale,
            "hazy preset should increase Mie density");
    require(atmosphere_config_for_preset(AtmospherePreset::ThinAir).rayleigh_density_scale <
                atmosphere_config_for_preset(AtmospherePreset::Noon).rayleigh_density_scale,
            "thin-air preset should reduce Rayleigh density");
    require(atmosphere_config_for_preset(AtmospherePreset::HighAltitude).camera_altitude_km >
                atmosphere_config_for_preset(AtmospherePreset::Noon).camera_altitude_km,
            "high-altitude preset should move the camera upward");
    require_throws([] { static_cast<void>(atmosphere_preset_from_name("storm")); },
                   "atmosphere preset parser should reject unknown presets");

    AtmosphereConfig defaults = atmosphere_config_for_preset(AtmospherePreset::Noon);
    require(defaults.bottom_radius_km > 0.0F && defaults.top_radius_km > defaults.bottom_radius_km,
            "default atmosphere radii should be positive and ordered");
    require(defaults.rayleigh_scale_height_km > 0.0F && defaults.mie_scale_height_km > 0.0F,
            "default atmosphere scale heights should be positive");
    require(defaults.mie_anisotropy >= 0.0F && defaults.mie_anisotropy < 1.0F,
            "default Mie anisotropy should be in [0, 1)");
    require(defaults.rayleigh_scattering.x >= 0.0F && defaults.rayleigh_scattering.y >= 0.0F &&
                defaults.rayleigh_scattering.z >= 0.0F && defaults.mie_scattering >= 0.0F &&
                defaults.mie_extinction >= 0.0F,
            "default scattering coefficients should be nonnegative");
    require(defaults.reference_geometry_enabled && defaults.reference_grid_km > 0.0F &&
                defaults.reference_intensity > 0.0F,
            "default atmosphere config should expose reference ground geometry");

    {
        AtmosphereConfig invalid = defaults;
        invalid.top_radius_km = invalid.bottom_radius_km;
        require_throws([&invalid] { validate_atmosphere_config(invalid); },
                       "atmosphere config should reject unordered radii");
    }
    {
        AtmosphereConfig invalid = defaults;
        invalid.mie_anisotropy = 1.0F;
        require_throws([&invalid] { validate_atmosphere_config(invalid); },
                       "atmosphere config should reject invalid Mie anisotropy");
    }
    {
        AtmosphereConfig invalid = defaults;
        invalid.reference_grid_km = 0.0F;
        require_throws([&invalid] { validate_atmosphere_config(invalid); },
                       "atmosphere config should reject invalid reference grid scale");
    }
    {
        cubey::RunConfig run_config;
        run_config.atmosphere.preset = "sunset";
        run_config.debug_view = "mie";
        run_config.atmosphere.sun_elevation_degrees = 6.0F;
        run_config.atmosphere.sun_azimuth_degrees = 33.0F;
        run_config.atmosphere.camera_altitude_km = 2.0F;
        run_config.atmosphere.mie_scale = 2.25F;
        AtmosphereConfig config = atmosphere_config_from_run_config(run_config);
        require(config.preset == AtmospherePreset::Sunset,
                "run config should select atmosphere preset");
        require(config.render_view == AtmosphereRenderView::Mie,
                "run config should select atmosphere debug view");
        require(config.sun_elevation_degrees == 6.0F && config.sun_azimuth_degrees == 33.0F &&
                    config.camera_altitude_km == 2.0F && config.mie_density_scale == 2.25F,
                "run config atmosphere overrides should win over preset defaults");
    }

    const std::filesystem::path source_root = CUBEY_ATMOSPHERE_SOURCE_DIR;
    const std::string app_source = read_text_file(source_root / "atmosphere_app.cpp");
    const std::string shader_source = read_text_file(source_root / "shaders/atmosphere.frag");
    require_contains(app_source, "static_assert(sizeof(AtmospherePushConstants)",
                     "atmosphere app should lock push constant size");
    require_contains(shader_source, "rayleigh_phase",
                     "atmosphere shader should include Rayleigh phase");
    require_contains(shader_source, "mie_phase", "atmosphere shader should include Mie phase");
    require_contains(shader_source, "ray_sphere_intersection",
                     "atmosphere shader should intersect atmosphere and ground spheres");
    require_contains(shader_source, "ozone_density",
                     "atmosphere shader should include ozone absorption density");
    require_contains(shader_source, "transmittance_from_depth",
                     "atmosphere shader should expose transmittance");
    require_contains(shader_source, "sun_visibility",
                     "atmosphere shader should soften low-sun planet shadow visibility");
    require_contains(shader_source, "ground_sun_visibility",
                     "atmosphere shader should soften low-sun ground lighting");
    require_contains(shader_source, "ATMOSPHERE_MIN_TWILIGHT_SOFTNESS",
                     "atmosphere shader should include a twilight terminator softness floor");
    require_contains(shader_source, "debug_view == 6",
                     "atmosphere shader should include aerial perspective debug output");
    require_contains(shader_source, "ground_reference_geometry",
                     "atmosphere shader should include ground reference geometry");
    require_contains(shader_source, "reference_line",
                     "atmosphere shader should include antialiased reference lines");
}
