#include "atmosphere_config.h"

#include <cubey/core/run_config.h>

#include <cmath>
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

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
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
                AtmosphereRenderView::NightSky,
            "atmosphere render view cycle should include night sky after aerial perspective");
    require(next_atmosphere_render_view(AtmosphereRenderView::NightSky) ==
                AtmosphereRenderView::Final,
            "atmosphere render view cycle should wrap");
    require_throws([] { static_cast<void>(atmosphere_render_view_from_name("density")); },
                   "atmosphere render view parser should reject unknown views");

    for (const SunControlMode mode : kSunControlModes) {
        require(sun_control_mode_from_name(sun_control_mode_name(mode)) == mode,
                "sun control mode names should round trip");
    }
    require(sun_control_mode_from_name("") == SunControlMode::SolarClock,
            "empty sun control mode should default to solar clock");
    require_throws([] { static_cast<void>(sun_control_mode_from_name("civil")); },
                   "sun control mode parser should reject unknown modes");

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
    require(atmosphere_config_for_preset(AtmospherePreset::Night).time_of_day.time_hours == 0.0F,
            "night preset should start at midnight");
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
    require(defaults.night_sky.twilight_strength > 0.0F &&
                defaults.night_sky.star_intensity > 0.0F &&
                defaults.night_sky.star_density > 0.0F && defaults.night_sky.star_density <= 1.0F,
            "default atmosphere config should include night sky controls");

    {
        TimeOfDayConfig solar_noon;
        solar_noon.time_hours = 12.0F;
        solar_noon.day_of_year = 80.0F;
        solar_noon.latitude_degrees = 30.0F;
        const SolarPosition position = atmosphere_solar_position(solar_noon);
        require_near(position.elevation_degrees, 60.0F, 0.2F,
                     "solar equinox noon at 30 degrees latitude should resolve near 60 degrees");
        require_near(position.azimuth_degrees, 0.0F, 0.2F,
                     "solar equinox noon should face scene south");
    }
    {
        TimeOfDayConfig morning;
        morning.time_hours = 9.0F;
        TimeOfDayConfig afternoon = morning;
        afternoon.time_hours = 15.0F;
        require(atmosphere_solar_position(morning).azimuth_degrees > 0.0F,
                "morning solar azimuth should be east-positive");
        require(atmosphere_solar_position(afternoon).azimuth_degrees < 0.0F,
                "afternoon solar azimuth should be west-negative");
    }
    {
        TimeOfDayConfig sunset;
        sunset.time_hours = 17.8F;
        const SolarPosition position = atmosphere_solar_position(sunset);
        require(position.elevation_degrees > -2.0F && position.elevation_degrees < 5.0F,
                "solar sunset preset time should resolve near the horizon");
    }
    {
        AtmosphereConfig clock = atmosphere_config_for_preset(AtmospherePreset::Noon);
        clock.time_of_day.time_hours = 23.5F;
        clock.time_of_day.day_of_year = 80.0F;
        clock.time_of_day.speed_hours_per_second = 2.0F;
        advance_atmosphere_time_of_day(clock, 0.5);
        require_near(clock.time_of_day.time_hours, 0.5F, 0.001F,
                     "atmosphere time playback should wrap across midnight");
        require_near(clock.time_of_day.day_of_year, 81.0F, 0.001F,
                     "atmosphere time playback should advance the day across midnight");
    }
    {
        AtmosphereConfig clock = atmosphere_config_for_preset(AtmospherePreset::Noon);
        clock.time_of_day.time_hours = 23.5F;
        clock.time_of_day.day_of_year = 366.0F;
        clock.time_of_day.speed_hours_per_second = 2.0F;
        advance_atmosphere_time_of_day(clock, 0.5);
        require_near(clock.time_of_day.day_of_year, 1.0F, 0.001F,
                     "atmosphere time playback should wrap day of year");
    }
    {
        TimeOfDayConfig sidereal;
        sidereal.time_hours = 0.0F;
        sidereal.day_of_year = 80.0F;
        const float midnight = atmosphere_sidereal_angle_radians(sidereal);
        sidereal.time_hours = 1.0F;
        const float one_hour = atmosphere_sidereal_angle_radians(sidereal);
        require_near(atmosphere_radians_to_degrees(one_hour - midnight), 15.041F, 0.002F,
                     "sidereal angle should advance at the sidereal hourly rate");
        sidereal.time_hours = 0.0F;
        sidereal.day_of_year = 81.0F;
        const float next_day = atmosphere_sidereal_angle_radians(sidereal);
        require_near(atmosphere_radians_to_degrees(next_day - midnight), 0.986F, 0.002F,
                     "sidereal angle should advance between solar days");
    }
    {
        require(atmosphere_auto_exposure(2.0F, 0.0F) > atmosphere_auto_exposure(60.0F, 0.0F),
                "auto exposure should brighten low sun relative to daylight");
        require(atmosphere_auto_exposure(-20.0F, 0.0F) > atmosphere_auto_exposure(-6.0F, 0.0F),
                "auto exposure should brighten full night beyond twilight");
        require(atmosphere_auto_exposure(-20.0F, 4.0F) <= 4.0F,
                "auto exposure should clamp to the existing exposure range");
    }

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
        AtmosphereConfig invalid = defaults;
        invalid.night_sky.star_density = 1.25F;
        require_throws([&invalid] { validate_atmosphere_config(invalid); },
                       "atmosphere config should reject invalid night sky controls");
    }
    {
        cubey::RunConfig run_config;
        run_config.atmosphere.preset = "sunset";
        run_config.debug_view = "night-sky";
        run_config.atmosphere.sun_elevation_degrees = 6.0F;
        run_config.atmosphere.sun_azimuth_degrees = 33.0F;
        run_config.atmosphere.camera_altitude_km = 2.0F;
        run_config.atmosphere.mie_scale = 2.25F;
        run_config.atmosphere.twilight_strength = 1.50F;
        run_config.atmosphere.twilight_horizon_warmth = 0.80F;
        run_config.atmosphere.star_intensity = 1.70F;
        run_config.atmosphere.star_density = 0.40F;
        AtmosphereConfig config = atmosphere_config_from_run_config(run_config);
        require(config.preset == AtmospherePreset::Sunset,
                "run config should select atmosphere preset");
        require(config.render_view == AtmosphereRenderView::NightSky,
                "run config should select atmosphere debug view");
        require(config.sun_elevation_degrees == 6.0F && config.sun_azimuth_degrees == 33.0F &&
                    config.camera_altitude_km == 2.0F && config.mie_density_scale == 2.25F,
                "run config atmosphere overrides should win over preset defaults");
        require(config.night_sky.twilight_strength == 1.50F &&
                    config.night_sky.twilight_horizon_warmth == 0.80F &&
                    config.night_sky.star_intensity == 1.70F &&
                    config.night_sky.star_density == 0.40F,
                "run config night sky overrides should win over preset defaults");
        require(config.time_of_day.mode == SunControlMode::ManualSun,
                "manual sun overrides should force manual sun mode");
        require(!config.time_of_day.auto_exposure_enabled,
                "manual sun mode should default to fixed exposure");
    }
    {
        cubey::RunConfig run_config;
        run_config.atmosphere.time_of_day_mode = "solar";
        run_config.atmosphere.time_hours = 17.8F;
        run_config.atmosphere.day_of_year = 80.0F;
        run_config.atmosphere.latitude_degrees = 30.0F;
        run_config.atmosphere.sun_azimuth_offset_degrees = 5.0F;
        run_config.atmosphere.time_speed_hours_per_second = 1.25F;
        run_config.atmosphere.exposure_bias = 0.5F;
        AtmosphereConfig config = atmosphere_config_from_run_config(run_config);
        require(config.time_of_day.mode == SunControlMode::SolarClock,
                "run config should select solar clock mode");
        require(config.time_of_day.time_hours == 17.8F && config.time_of_day.day_of_year == 80.0F &&
                    config.time_of_day.latitude_degrees == 30.0F &&
                    config.time_of_day.azimuth_offset_degrees == 5.0F &&
                    config.time_of_day.speed_hours_per_second == 1.25F,
                "run config should apply solar clock overrides");
        require(config.sun_elevation_degrees > -2.0F && config.sun_elevation_degrees < 5.0F,
                "solar clock config should resolve sun elevation");
        require(config.exposure > 0.0F, "solar clock config should resolve auto exposure");
    }
    {
        cubey::RunConfig run_config;
        run_config.atmosphere.time_of_day_mode = "solar";
        run_config.pbr.exposure = -1.25F;
        run_config.pbr.exposure_explicit = true;
        AtmosphereConfig config = atmosphere_config_from_run_config(run_config);
        require(!config.time_of_day.auto_exposure_enabled,
                "explicit exposure should disable auto exposure");
        require(config.exposure == -1.25F, "explicit exposure should become fixed exposure");
    }

    const std::filesystem::path source_root = CUBEY_ATMOSPHERE_SOURCE_DIR;
    const std::string app_source = read_text_file(source_root / "atmosphere_app.cpp");
    const std::string shader_source = read_text_file(source_root / "shaders/atmosphere.frag");
    require_contains(app_source, "static_assert(sizeof(AtmospherePushConstants)",
                     "atmosphere app should lock push constant size");
    require_contains(app_source, "sizeof(float) * 48U",
                     "atmosphere app should include celestial push constants");
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
    require_contains(shader_source, "twilight_radiance",
                     "atmosphere shader should include twilight radiance");
    require_contains(shader_source, "procedural_star_radiance",
                     "atmosphere shader should include procedural stars");
    require_contains(shader_source, "star_sample_direction",
                     "atmosphere shader should rotate star sampling through celestial space");
    require_contains(shader_source, "celestial_options",
                     "atmosphere shader should receive celestial rotation options");
    require_contains(shader_source, "debug_view == 7",
                     "atmosphere shader should include night sky debug output");
    require_contains(shader_source, "ground_reference_geometry",
                     "atmosphere shader should include ground reference geometry");
    require_contains(shader_source, "reference_line",
                     "atmosphere shader should include antialiased reference lines");
}
