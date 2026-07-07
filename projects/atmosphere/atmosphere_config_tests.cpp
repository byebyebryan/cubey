#include "atmosphere_config.h"
#include "atmosphere_environment.h"

#include <cubey/core/run_config.h>
#include <cubey/render/atmosphere_night_sky_atlas.h>
#include <cubey/render/lunar_surface_map.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

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

void require_not_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) != std::string::npos) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] const std::uint8_t* lunar_surface_map_texel(const cubey::render::LunarSurfaceMap& map,
                                                          std::uint32_t x, std::uint32_t y,
                                                          std::uint32_t mip = 0) {
    const cubey::render::LunarSurfaceMapMip& level = map.mips.at(mip);
    const std::size_t texel =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(level.width) + x) * 4U;
    return map.rgba8.data() + level.byte_offset + texel;
}

struct TestVec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] float test_dot(TestVec3 lhs, TestVec3 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] TestVec3 test_cross(TestVec3 lhs, TestVec3 rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] TestVec3 test_normalize(TestVec3 value) {
    const float len = std::sqrt(test_dot(value, value));
    return {value.x / len, value.y / len, value.z / len};
}

[[nodiscard]] TestVec3 test_add(TestVec3 lhs, TestVec3 rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] TestVec3 test_sub(TestVec3 lhs, TestVec3 rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] TestVec3 test_mul(TestVec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] TestVec3 test_galactic_direction(TestVec3 center, TestVec3 tangent, TestVec3 pole,
                                               float longitude, float latitude) {
    const float horizontal = std::cos(latitude);
    return test_normalize(test_add(test_add(test_mul(center, std::cos(longitude) * horizontal),
                                            test_mul(tangent, std::sin(longitude) * horizontal)),
                                   test_mul(pole, std::sin(latitude))));
}

[[nodiscard]] float night_sky_luminance_at(const cubey::render::NightSkyAtlas& atlas,
                                           TestVec3 direction) {
    direction = test_normalize(direction);
    const TestVec3 axis{std::abs(direction.x), std::abs(direction.y), std::abs(direction.z)};
    std::uint32_t face = 0;
    float u = 0.0F;
    float v = 0.0F;
    if (axis.x >= axis.y && axis.x >= axis.z) {
        face = direction.x > 0.0F ? 0U : 1U;
        u = direction.x > 0.0F ? -direction.z / axis.x : direction.z / axis.x;
        v = -direction.y / axis.x;
    } else if (axis.y >= axis.z) {
        face = direction.y > 0.0F ? 2U : 3U;
        u = direction.x / axis.y;
        v = direction.y > 0.0F ? direction.z / axis.y : -direction.z / axis.y;
    } else {
        face = direction.z > 0.0F ? 4U : 5U;
        u = direction.z > 0.0F ? direction.x / axis.z : -direction.x / axis.z;
        v = -direction.y / axis.z;
    }
    const std::uint32_t extent = atlas.mips.at(0).extent;
    const auto to_index = [extent](float value) {
        const float scaled = ((value * 0.5F + 0.5F) * static_cast<float>(extent));
        return static_cast<std::uint32_t>(
            std::clamp(static_cast<int>(std::floor(scaled)), 0, static_cast<int>(extent) - 1));
    };
    const std::uint32_t x = to_index(u);
    const std::uint32_t y = to_index(v);
    const std::size_t offset = (static_cast<std::size_t>(face) * extent * extent +
                                static_cast<std::size_t>(y) * extent + x) *
                               4U;
    return atlas.rgba32f[offset] * 0.2126F + atlas.rgba32f[offset + 1U] * 0.7152F +
           atlas.rgba32f[offset + 2U] * 0.0722F;
}

struct AtlasLuminanceStats {
    float min = std::numeric_limits<float>::max();
    float max = 0.0F;
    float sum = 0.0F;
    std::size_t above_threshold = 0;
};

[[nodiscard]] AtlasLuminanceStats
night_sky_luminance_stats(const cubey::render::NightSkyAtlas& atlas, float threshold) {
    AtlasLuminanceStats stats;
    for (std::size_t index = 0; index + 2U < atlas.rgba32f.size(); index += 4U) {
        const float luma = atlas.rgba32f[index] * 0.2126F + atlas.rgba32f[index + 1U] * 0.7152F +
                           atlas.rgba32f[index + 2U] * 0.0722F;
        stats.min = std::min(stats.min, luma);
        stats.max = std::max(stats.max, luma);
        stats.sum += luma;
        if (luma > threshold) {
            ++stats.above_threshold;
        }
    }
    return stats;
}

} // namespace

int main() {
    using namespace cubey::projects::atmosphere;
    using namespace cubey::render;

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
                AtmosphereRenderView::MilkyWay,
            "atmosphere render view cycle should include Milky Way after night sky");
    require(next_atmosphere_render_view(AtmosphereRenderView::MilkyWay) ==
                AtmosphereRenderView::Moon,
            "atmosphere render view cycle should include moon after Milky Way");
    require(next_atmosphere_render_view(AtmosphereRenderView::Moon) ==
                AtmosphereRenderView::MoonSurface,
            "atmosphere render view cycle should include moon surface after moon");
    require(next_atmosphere_render_view(AtmosphereRenderView::MoonSurface) ==
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

    for (const NightSkyVisualMode mode : kNightSkyVisualModes) {
        require(night_sky_visual_mode_from_name(night_sky_visual_mode_name(mode)) == mode,
                "night sky visual mode names should round trip");
    }
    require(night_sky_visual_mode_from_name("") == NightSkyVisualMode::HumanEye,
            "empty night sky visual mode should default to human eye");
    require_throws([] { static_cast<void>(night_sky_visual_mode_from_name("neon")); },
                   "night sky visual mode parser should reject unknown modes");

    for (const NightSkyLayerView layer : kNightSkyLayerViews) {
        require(night_sky_layer_view_from_name(night_sky_layer_view_name(layer)) == layer,
                "night sky layer view names should round trip");
    }
    require(night_sky_layer_view_from_name("") == NightSkyLayerView::Final,
            "empty night sky layer should default to final");
    require_throws([] { static_cast<void>(night_sky_layer_view_from_name("hydrogen")); },
                   "night sky layer parser should reject unknown layers");

    for (const cubey::render::AtmosphereEnvironmentGroundMode mode : kAtmosphereGroundModes) {
        require(atmosphere_ground_mode_from_name(atmosphere_ground_mode_name(mode)) == mode,
                "atmosphere ground mode names should round trip");
    }
    require(atmosphere_ground_mode_from_name("") ==
                cubey::render::AtmosphereEnvironmentGroundMode::Ground,
            "empty atmosphere ground mode should default to ground");
    require_throws([] { static_cast<void>(atmosphere_ground_mode_from_name("underground")); },
                   "atmosphere ground mode parser should reject unknown modes");

    for (const AtmosphereCloudWeatherPreset preset : kAtmosphereCloudWeatherPresets) {
        require(atmosphere_cloud_weather_preset_from_name(
                    atmosphere_cloud_weather_preset_name(preset)) == preset,
                "atmosphere cloud weather preset names should round trip");
    }
    require(atmosphere_cloud_weather_preset_from_name("storm") ==
                AtmosphereCloudWeatherPreset::StormCells,
            "legacy storm cloud weather preset alias should parse");
    require(atmosphere_cloud_weather_preset_from_name("inspection") ==
                AtmosphereCloudWeatherPreset::BrokenCumulus,
            "inspection cloud weather preset alias should parse");
    require(atmosphere_cloud_weather_preset_from_name("cloud-ref-parity") ==
                AtmosphereCloudWeatherPreset::SurfaceVolume,
            "cloud reference parity preset alias should map to surface volume");
    require(atmosphere_cloud_weather_preset_from_name("surface-volume") ==
                AtmosphereCloudWeatherPreset::SurfaceVolume,
            "surface volume cloud weather preset should parse");
    require_throws(
        [] { static_cast<void>(atmosphere_cloud_weather_preset_from_name("planetary-storm")); },
        "atmosphere cloud weather preset parser should reject unknown presets");

    for (const CloudLayerDebugView view : kCloudLayerDebugViews) {
        require(cloud_layer_debug_view_from_name(cloud_layer_debug_view_name(view)) == view,
                "cloud layer debug view names should round trip");
    }
    require(next_cloud_layer_debug_view(CloudLayerDebugView::Final) ==
                CloudLayerDebugView::RawFinal,
            "cloud layer debug view should expose raw final after final");
    require(next_cloud_layer_debug_view(CloudLayerDebugView::Lighting) ==
                CloudLayerDebugView::AmbientLight,
            "cloud layer debug view should include ambient-light diagnostics");
    require(next_cloud_layer_debug_view(CloudLayerDebugView::OrbitShellShadow) ==
                CloudLayerDebugView::OrbitShellFootprint,
            "cloud layer debug view should include shell footprint diagnostics");
    require(next_cloud_layer_debug_view(CloudLayerDebugView::OrbitShellMass) ==
                CloudLayerDebugView::JitterPattern,
            "cloud layer debug view should include sampling diagnostics");
    require(next_cloud_layer_debug_view(CloudLayerDebugView::HorizonFilterLod) ==
                CloudLayerDebugView::HorizonHandoff,
            "cloud layer debug view should include horizon handoff diagnostics");
    require(next_cloud_layer_debug_view(CloudLayerDebugView::IntegratedHorizonRadiance) ==
                CloudLayerDebugView::EdgeMask,
            "cloud layer debug view should include integrated horizon diagnostics");
    require(next_cloud_layer_debug_view(CloudLayerDebugView::EdgeMask) ==
                CloudLayerDebugView::SceneDepthOcclusion,
            "cloud layer debug view should include edge resolve diagnostics");
    require(next_cloud_layer_debug_view(CloudLayerDebugView::SceneDepthOcclusion) ==
                CloudLayerDebugView::Final,
            "cloud layer debug view should wrap");
    require(cloud_layer_debug_view_from_name("weather") == CloudLayerDebugView::AuthoredWeather,
            "cloud weather debug alias should parse");
    require(cloud_layer_debug_view_from_name("weather-mask") == CloudLayerDebugView::CoverageBias,
            "cloud weather mask debug alias should parse");
    require(cloud_layer_debug_view_from_name("orbit-weather") == CloudLayerDebugView::OrbitCoverage,
            "cloud orbit weather debug alias should parse");
    require(cloud_layer_debug_view_from_name("shell-normal") ==
                CloudLayerDebugView::OrbitShellNormal,
            "cloud shell normal debug alias should parse");
    require(cloud_layer_debug_view_from_name("shell-footprint") ==
                CloudLayerDebugView::OrbitShellFootprint,
            "cloud shell footprint debug alias should parse");
    require(cloud_layer_debug_view_from_name("shell-filter") ==
                CloudLayerDebugView::OrbitShellFilter,
            "cloud shell filter debug alias should parse");
    require(cloud_layer_debug_view_from_name("shell-mass") == CloudLayerDebugView::OrbitShellMass,
            "cloud shell mass debug alias should parse");
    require(cloud_layer_debug_view_from_name("jitter") == CloudLayerDebugView::JitterPattern,
            "cloud jitter debug alias should parse");
    require(cloud_layer_debug_view_from_name("horizon-lod") ==
                CloudLayerDebugView::HorizonFilterLod,
            "cloud horizon LOD debug alias should parse");
    require(cloud_layer_debug_view_from_name("horizon-gate") == CloudLayerDebugView::HorizonHandoff,
            "cloud horizon handoff debug alias should parse");
    require(cloud_layer_debug_view_from_name("horizon-local-truncation") ==
                CloudLayerDebugView::LocalTruncation,
            "cloud local truncation debug alias should parse");
    require(cloud_layer_debug_view_from_name("horizon-integrated-alpha") ==
                CloudLayerDebugView::IntegratedHorizonAlpha,
            "cloud integrated horizon alpha debug alias should parse");
    require(cloud_layer_debug_view_from_name("horizon-integrated-radiance") ==
                CloudLayerDebugView::IntegratedHorizonRadiance,
            "cloud integrated horizon radiance debug alias should parse");
    require(cloud_layer_debug_view_from_name("depth-mask") ==
                CloudLayerDebugView::SceneDepthOcclusion,
            "cloud scene depth debug alias should parse");
    require_throws([] { static_cast<void>(cloud_layer_debug_view_from_name("humidity")); },
                   "cloud layer debug view parser should reject unknown views");
    require(atmosphere_cloud_sampling_mode_from_name("") == CloudLayerSamplingMode::Bayer,
            "empty cloud sampling mode should default to stable Bayer");
    require(atmosphere_cloud_sampling_mode_from_name("blue-noise") ==
                CloudLayerSamplingMode::BlueNoise,
            "blue-noise cloud sampling mode should parse");
    require(atmosphere_cloud_view_sample_mode_from_name("") ==
                CloudLayerViewSampleMode::SingleFrame,
            "empty cloud view sample mode should default to single-frame");
    require(atmosphere_cloud_view_sample_mode_from_name("temporal-phased") ==
                CloudLayerViewSampleMode::TemporalPhased,
            "temporal phased cloud view sample mode should parse");
    require(atmosphere_cloud_density_model_from_name("") == CloudLayerDensityModel::SurfaceVolume,
            "empty cloud density model should default to surface-volume");
    require(atmosphere_cloud_density_model_from_name("surface-volume") ==
                CloudLayerDensityModel::SurfaceVolume,
            "surface-volume cloud density model should parse");
    require(atmosphere_cloud_density_model_from_name("procedural") ==
                CloudLayerDensityModel::ExperimentalAerialOrbit,
            "legacy procedural cloud density model should map to experimental aerial/orbit");
    require(atmosphere_cloud_density_model_from_name("experimental-aerial-orbit") ==
                CloudLayerDensityModel::ExperimentalAerialOrbit,
            "experimental aerial/orbit cloud density model should parse");
    require(atmosphere_cloud_density_model_from_name("ref-density") ==
                CloudLayerDensityModel::RefDensity,
            "ref-density cloud density model should parse");
    require(atmosphere_cloud_density_model_from_name("terrain-ref") ==
                CloudLayerDensityModel::RefDensity,
            "legacy terrain-ref cloud density model alias should parse");
    require(atmosphere_cloud_density_model_from_name("cloud-ref-compatible") ==
                CloudLayerDensityModel::SurfaceVolume,
            "legacy cloud-ref-compatible density model should map to surface volume");
    require(atmosphere_cloud_resolve_mode_from_name("") == CloudLayerResolveMode::TerrainPost,
            "empty cloud resolve mode should default to terrain post");
    require(atmosphere_cloud_resolve_mode_from_name("metadata-bilateral") ==
                CloudLayerResolveMode::MetadataBilateral,
            "metadata-bilateral cloud resolve mode should parse");
    require(atmosphere_cloud_resolve_mode_from_name("gaussian") ==
                CloudLayerResolveMode::TerrainPost,
            "terrain post cloud resolve alias should parse");

    {
        const TimeOfDayConfig defaults;
        require_near(defaults.time_hours, 5.5F, 0.0001F,
                     "default atmosphere project time should start just before dawn");
        require_near(defaults.speed_hours_per_second, 0.5F, 0.0001F,
                     "default atmosphere project time speed should be half an hour per second");
        require_near(defaults.azimuth_offset_degrees, -10.0F, 0.0001F,
                     "default atmosphere project view should offset sunrise from straight-on");
    }

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
    require(atmosphere_config_for_preset(AtmospherePreset::MoonlitNight).moon.moonlight_intensity >
                atmosphere_config_for_preset(AtmospherePreset::Night).moon.moonlight_intensity,
            "moonlit night preset should increase moonlight");
    require(sizeof(AtmosphereFrameUniforms) == sizeof(float) * 72U,
            "atmosphere frame uniforms should keep the shader vec4 layout size");
    {
        AtmosphereConfig config = atmosphere_config_for_preset(AtmospherePreset::Noon);
        config.sun_elevation_degrees = 30.0F;
        config.sun_azimuth_degrees = 90.0F;
        const cubey::math::Vec3 sun = atmosphere_sun_direction(config);
        require_near(sun.x, std::cos(atmosphere_degrees_to_radians(30.0F)), 0.0001F,
                     "atmosphere sun direction should resolve azimuth around Y");
        require_near(sun.y, 0.5F, 0.0001F, "atmosphere sun direction should resolve elevation");
        require(std::abs(sun.z) < 0.0001F,
                "atmosphere sun direction should face the requested azimuth");
    }
    {
        AtmosphereConfig config = atmosphere_config_for_preset(AtmospherePreset::MoonlitNight);
        const cubey::render::ViewRayBasis3D view_rays = cubey::render::view_ray_basis_3d(
            cubey::math::identity_quat(), 1.5F, std::numbers::pi_v<float> * 0.5F);
        const AtmosphereFrameUniforms uniforms =
            atmosphere_frame_uniforms(config, {
                                                  .view_rays = view_rays,
                                                  .render_view = AtmosphereRenderView::Moon,
                                              });
        require(uniforms.camera_right_aspect == view_rays.right_aspect,
                "frame uniforms should preserve packed view ray right/aspect");
        require(uniforms.camera_up_tan_half_fovy == view_rays.up_tan_half_fovy,
                "frame uniforms should preserve packed view ray up/fovy");
        require_near(uniforms.camera_forward_debug_view.z, -1.0F, 0.0001F,
                     "frame uniforms should preserve packed forward ray");
        require(uniforms.camera_forward_debug_view.w ==
                    static_cast<float>(static_cast<std::uint32_t>(AtmosphereRenderView::Moon)),
                "frame uniforms should pack the debug render view");
        require(uniforms.moon_options.x == 1.0F, "frame uniforms should pack the moon enable flag");
        require(uniforms.render_options.x == 2.0F,
                "frame uniforms should use clean sky/cloud review mode by default");
        require(uniforms.render_options.y == 1.0F,
                "frame uniforms should render inline celestial content by default");
        require(uniforms.celestial_render_options.x == 1.0F,
                "frame uniforms should render the inline sun disk by default");
        require(uniforms.celestial_render_options.y == 1.0F,
                "frame uniforms should render inline night sky by default");
        require(uniforms.celestial_render_options.z == 1.0F,
                "frame uniforms should render the inline moon disk by default");
        require(uniforms.celestial_options.z ==
                    std::sin(atmosphere_degrees_to_radians(config.time_of_day.latitude_degrees)),
                "frame uniforms should pack the observer latitude sine");
    }
    {
        AtmosphereConfig config = atmosphere_config_for_preset(AtmospherePreset::Noon);
        config.render_celestial_content = false;
        const cubey::render::ViewRayBasis3D view_rays = cubey::render::view_ray_basis_3d(
            cubey::math::identity_quat(), 1.0F, std::numbers::pi_v<float> * 0.5F);
        const AtmosphereFrameUniforms uniforms =
            atmosphere_frame_uniforms(config, {
                                                  .view_rays = view_rays,
                                              });
        require(uniforms.render_options.y == 0.0F,
                "frame uniforms should expose inline celestial content suppression");
    }
    {
        AtmosphereConfig config = atmosphere_config_for_preset(AtmospherePreset::Noon);
        config.render_sun_disk = false;
        config.render_night_sky = true;
        config.render_moon_disk = false;
        const cubey::render::ViewRayBasis3D view_rays = cubey::render::view_ray_basis_3d(
            cubey::math::identity_quat(), 1.0F, std::numbers::pi_v<float> * 0.5F);
        const AtmosphereFrameUniforms uniforms =
            atmosphere_frame_uniforms(config, {
                                                  .view_rays = view_rays,
                                              });
        require(uniforms.celestial_render_options.x == 0.0F,
                "frame uniforms should expose sun disk suppression");
        require(uniforms.celestial_render_options.y == 1.0F,
                "frame uniforms should expose night sky rendering");
        require(uniforms.celestial_render_options.z == 0.0F,
                "frame uniforms should expose moon disk suppression");
    }
    {
        AtmosphereConfig moonlit = atmosphere_config_for_preset(AtmospherePreset::MoonlitNight);
        moonlit.time_of_day.azimuth_offset_degrees = 0.0F;
        resolve_atmosphere_time_of_day(moonlit);
        require(moonlit.sun_elevation_degrees < -30.0F,
                "moonlit night preset should resolve below astronomical twilight");
        require_near(moonlit.exposure, 2.4F, 0.05F,
                     "moonlit night preset should use exposure bias for its resolved exposure");
        const LunarState moonlit_moon = atmosphere_lunar_state(moonlit.time_of_day, moonlit.moon);
        require(moonlit_moon.illumination > 0.90F,
                "moonlit night preset should keep the moon mostly illuminated");
        require(std::abs(moonlit_moon.direction.x) < 0.02F && moonlit_moon.direction.y > 0.35F &&
                    moonlit_moon.direction.y < 0.45F,
                "moonlit night preset should place the moon in the default horizon view");
    }
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
    require(!defaults.reference_geometry_enabled && defaults.reference_grid_km > 0.0F &&
                defaults.reference_intensity > 0.0F,
            "default atmosphere config should keep reference ground geometry available but hidden");
    require(defaults.ground_mode ==
                cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
            "default atmosphere config should use clean sky/cloud review rendering");
    require(defaults.night_sky.twilight_strength > 0.0F &&
                defaults.night_sky.star_intensity > 0.0F &&
                defaults.night_sky.star_density > 0.0F && defaults.night_sky.star_density <= 1.0F &&
                defaults.night_sky.milky_way_intensity > 0.0F &&
                defaults.night_sky.milky_way_contrast > 0.0F,
            "default atmosphere config should include night sky controls");
    require(defaults.moon.enabled && defaults.moon.disk_intensity > 0.0F &&
                defaults.moon.moonlight_intensity > 0.0F &&
                defaults.moon.phase_offset_days > 0.0F && defaults.moon.angular_radius_scale > 0.0F,
            "default atmosphere config should include moon controls");
    require(defaults.clouds.enabled &&
                defaults.clouds.weather_preset == AtmosphereCloudWeatherPreset::SurfaceVolume &&
                defaults.clouds.layer.debug_view == CloudLayerDebugView::Final,
            "default atmosphere config should include production clouds");
    require(defaults.clouds.layer.sampling_mode == CloudLayerSamplingMode::Bayer &&
                !defaults.clouds.layer.temporal_enabled,
            "default atmosphere clouds should use stable Bayer sampling");
    require_near(defaults.clouds.layer.jitter_strength, 1.0F, 0.001F,
                 "default atmosphere clouds should use full jitter to reduce banding");
    require(
        defaults.clouds.layer.view_steps_override == 64 &&
            defaults.clouds.layer.view_samples == 1 &&
            defaults.clouds.layer.view_sample_mode == CloudLayerViewSampleMode::SingleFrame,
        "default atmosphere clouds should use surface reference steps and one single-frame sample");
    require(defaults.clouds.layer.distance_mode == CloudLayerDistanceMode::Local,
            "default atmosphere clouds should use the stable local surface path");
    require(defaults.clouds.layer.density_model == CloudLayerDensityModel::SurfaceVolume,
            "default atmosphere clouds should use the accepted surface-volume density model");
    require(defaults.clouds.layer.quality == CloudLayerQuality::Full &&
                defaults.clouds.layer.resolve_mode == CloudLayerResolveMode::TerrainPost &&
                !defaults.clouds.layer.horizon_layer_enabled,
            "default atmosphere clouds should keep the stable production surface defaults");
    require_near(defaults.clouds.layer.top_altitude_m, 14000.0F, 0.001F,
                 "default atmosphere clouds should use the accepted lower surface cloud ceiling");
    require_near(defaults.clouds.layer.twilight_color_strength, 0.85F, 0.001F,
                 "default atmosphere clouds should include twilight color controls");
    require_near(defaults.clouds.layer.twilight_edge_strength, 0.55F, 0.001F,
                 "default atmosphere clouds should include twilight edge controls");
    require_near(defaults.clouds.layer.twilight_saturation_strength, 0.90F, 0.001F,
                 "default atmosphere clouds should include twilight saturation controls");
    require_near(defaults.clouds.layer.afterglow_strength, 0.32F, 0.001F,
                 "default atmosphere clouds should include a subtle afterglow control");
    require_near(defaults.clouds.layer.powder_strength, 0.20F, 0.001F,
                 "default atmosphere clouds should expose scalar powder strength");

    {
        AtmosphereCloudConfig clouds;
        apply_atmosphere_cloud_weather_preset(clouds, AtmosphereCloudWeatherPreset::StormCells);
        require(clouds.weather_preset == AtmosphereCloudWeatherPreset::StormCells,
                "atmosphere cloud weather preset should preserve selected preset");
        require(clouds.layer.cloud_style == CloudLayerCloudStyle::StormCells,
                "atmosphere cloud weather preset should select cloud style");
        require_near(clouds.layer.coverage, 0.64F, 0.001F,
                     "atmosphere cloud weather preset should apply coverage");
        require_near(clouds.layer.shape_domain_km, 525.0F, 0.001F,
                     "atmosphere cloud weather preset should apply shape domain");
        require_near(clouds.wind_speed_mps, 650.0F, 0.001F,
                     "atmosphere cloud weather preset should apply wind");
    }
    {
        AtmosphereCloudConfig clouds;
        apply_atmosphere_cloud_weather_preset(clouds, AtmosphereCloudWeatherPreset::SurfaceVolume);
        require(clouds.weather_preset == AtmosphereCloudWeatherPreset::SurfaceVolume,
                "surface volume preset should preserve selected preset");
        require(clouds.layer.quality == CloudLayerQuality::Full,
                "surface volume preset should force full-quality local comparison");
        require(clouds.layer.distance_mode == CloudLayerDistanceMode::Local,
                "surface volume preset should force local cloud distance mode");
        require(clouds.layer.density_model == CloudLayerDensityModel::SurfaceVolume,
                "surface volume preset should use the accepted density path");
        require(clouds.layer.resolve_mode == CloudLayerResolveMode::TerrainPost,
                "surface volume preset should use terrain-post resolve");
        require(!clouds.layer.temporal_enabled && clouds.layer.local_volume_enabled &&
                    !clouds.layer.horizon_layer_enabled,
                "surface volume preset should isolate the local volume path");
        require(clouds.layer.view_steps_override == 64 && clouds.layer.view_samples == 1,
                "surface volume preset should match cloud_ref full-quality sampling");
        require_near(clouds.layer.jitter_strength, 1.0F, 0.001F,
                     "surface volume preset should keep full jitter to reduce banding");
        require_near(clouds.layer.top_altitude_m, 14000.0F, 0.001F,
                     "surface volume preset should use the lower cloud_ref ceiling");
        require_near(clouds.layer.shadow_strength, 0.15F, 0.001F,
                     "surface volume preset should use the cloud_ref fair-weather shadow");
        require_near(clouds.layer.twilight_color_strength, 0.85F, 0.001F,
                     "surface volume preset should strengthen twilight cloud color");
        require_near(clouds.layer.twilight_edge_strength, 0.55F, 0.001F,
                     "surface volume preset should strengthen twilight cloud edges");
        require_near(clouds.layer.twilight_saturation_strength, 0.90F, 0.001F,
                     "surface volume preset should preserve twilight cloud saturation");
        require_near(clouds.layer.afterglow_strength, 0.32F, 0.001F,
                     "surface volume preset should expose a subtle afterglow accent");
        require_near(clouds.layer.powder_strength, 0.20F, 0.001F,
                     "surface volume preset should expose scalar powder strength");
    }

    {
        cubey::RunConfig run_config{};
        run_config.clouds.enabled = 0;
        run_config.clouds.debug_view = "orbit-weather";
        run_config.clouds.weather_preset = "storm";
        run_config.clouds.quality = "quarter";
        run_config.clouds.view_steps = 64;
        run_config.clouds.view_samples = 2;
        run_config.clouds.view_sample_mode = "temporal-phased";
        run_config.clouds.sampling_mode = "off";
        run_config.clouds.distance_mode = "orbit-shell";
        run_config.clouds.orbit_representation = "volume";
        run_config.clouds.density_model = "procedural";
        run_config.clouds.resolve_mode = "metadata-bilateral";
        run_config.clouds.planet_radius_m = 700000.0F;
        run_config.clouds.coverage = 0.25F;
        run_config.clouds.shape_domain_km = 840.0F;
        run_config.clouds.footprint_filter_strength = 1.35F;
        run_config.clouds.edge_softness = 1.20F;
        run_config.clouds.edge_detail_fade = 0.55F;
        run_config.clouds.edge_resolve_strength = 0.80F;
        run_config.clouds.wind_speed_mps = 42.0F;
        run_config.clouds.twilight_color_strength = 1.10F;
        run_config.clouds.twilight_edge_strength = 0.95F;
        run_config.clouds.twilight_saturation_strength = 1.25F;
        run_config.clouds.afterglow_strength = 0.70F;
        run_config.clouds.powder_strength = 0.45F;
        run_config.clouds.temporal = 0;
        run_config.clouds.local_volume = 0;
        run_config.clouds.horizon_layer = 1;
        const AtmosphereConfig config = atmosphere_config_from_run_config(run_config);
        require(!config.clouds.enabled, "atmosphere run config should disable clouds");
        require(config.clouds.weather_preset == AtmosphereCloudWeatherPreset::StormCells,
                "atmosphere run config should map cloud weather preset");
        require(config.clouds.layer.debug_view == CloudLayerDebugView::OrbitCoverage,
                "atmosphere run config should map cloud debug view");
        require(config.clouds.layer.quality == CloudLayerQuality::Quarter,
                "atmosphere run config should map cloud quality");
        require(config.clouds.layer.view_steps_override == 64,
                "atmosphere run config should map cloud view steps");
        require(config.clouds.layer.view_samples == 2,
                "atmosphere run config should map cloud view samples");
        require(config.clouds.layer.view_sample_mode == CloudLayerViewSampleMode::TemporalPhased,
                "atmosphere run config should map cloud view sample mode");
        require(config.clouds.layer.sampling_mode == CloudLayerSamplingMode::Off,
                "atmosphere run config should map cloud sampling");
        require(config.clouds.layer.distance_mode == CloudLayerDistanceMode::OrbitShell,
                "atmosphere run config should map cloud distance mode");
        require(config.clouds.layer.orbit_representation ==
                    CloudLayerOrbitRepresentation::VolumeRaymarch,
                "atmosphere run config should map cloud orbit representation");
        require(config.clouds.layer.density_model ==
                    CloudLayerDensityModel::ExperimentalAerialOrbit,
                "atmosphere run config should map legacy procedural density to the experimental "
                "aerial/orbit path");
        require(config.clouds.layer.resolve_mode == CloudLayerResolveMode::MetadataBilateral,
                "atmosphere run config should map cloud resolve mode");
        require_near(config.clouds.layer.planet_radius_m, 700000.0F, 0.001F,
                     "atmosphere run config should map cloud planet radius");
        require_near(config.clouds.layer.coverage, 0.25F, 0.001F,
                     "atmosphere run config explicit cloud coverage should override preset");
        require_near(config.clouds.layer.shape_domain_km, 840.0F, 0.001F,
                     "atmosphere run config should map cloud shape domain");
        require_near(config.clouds.layer.footprint_filter_strength, 1.35F, 0.001F,
                     "atmosphere run config should map cloud footprint filter strength");
        require_near(config.clouds.layer.edge_softness, 1.20F, 0.001F,
                     "atmosphere run config should map cloud edge softness");
        require_near(config.clouds.layer.edge_detail_fade, 0.55F, 0.001F,
                     "atmosphere run config should map cloud edge detail fade");
        require_near(config.clouds.layer.edge_resolve_strength, 0.80F, 0.001F,
                     "atmosphere run config should map cloud edge resolve strength");
        require_near(config.clouds.wind_speed_mps, 42.0F, 0.001F,
                     "atmosphere run config explicit cloud wind should override preset");
        require_near(config.clouds.layer.twilight_color_strength, 1.10F, 0.001F,
                     "atmosphere run config should map cloud twilight color strength");
        require_near(config.clouds.layer.twilight_edge_strength, 0.95F, 0.001F,
                     "atmosphere run config should map cloud twilight edge strength");
        require_near(config.clouds.layer.twilight_saturation_strength, 1.25F, 0.001F,
                     "atmosphere run config should map cloud twilight saturation strength");
        require_near(config.clouds.layer.afterglow_strength, 0.70F, 0.001F,
                     "atmosphere run config should map cloud afterglow strength");
        require_near(config.clouds.layer.powder_strength, 0.45F, 0.001F,
                     "atmosphere run config should map cloud powder strength");
        require(!config.clouds.layer.temporal_enabled,
                "atmosphere run config should map cloud temporal flag");
        require(!config.clouds.layer.local_volume_enabled,
                "atmosphere run config should map local volume flag");
        require(config.clouds.layer.horizon_layer_enabled,
                "atmosphere run config should map horizon layer flag");
    }
    {
        cubey::RunConfig run_config{};
        run_config.clouds.weather_preset = "reference-parity";
        run_config.clouds.quality = "half";
        run_config.clouds.distance_mode = "auto";
        run_config.clouds.horizon_layer = 1;
        run_config.clouds.view_steps = 48;
        const AtmosphereConfig config = atmosphere_config_from_run_config(run_config);
        require(
            config.clouds.weather_preset == AtmosphereCloudWeatherPreset::SurfaceVolume,
            "atmosphere run config should map legacy reference parity preset to surface volume");
        require(config.clouds.layer.quality == CloudLayerQuality::Half,
                "explicit cloud quality should override reference parity preset");
        require(config.clouds.layer.distance_mode == CloudLayerDistanceMode::Auto,
                "explicit cloud distance mode should override reference parity preset");
        require(config.clouds.layer.horizon_layer_enabled,
                "explicit horizon layer flag should override reference parity preset");
        require(config.clouds.layer.view_steps_override == 48,
                "explicit view steps should override reference parity preset");
        require(config.clouds.layer.density_model == CloudLayerDensityModel::SurfaceVolume,
                "surface volume preset should keep surface-volume density by default");
    }

    {
        require(lunar_surface_map_mip_count(kLunarSurfaceMapWidth, kLunarSurfaceMapHeight) == 11U,
                "default lunar surface map should include a complete 2:1 mip chain");
        require_throws([] { static_cast<void>(lunar_surface_map_mip_count(512U, 512U)); },
                       "lunar surface map should reject non-equirectangular dimensions");

        const LunarSurfaceMap default_map = generate_lunar_surface_map();
        require(default_map.width == kLunarSurfaceMapWidth &&
                    default_map.height == kLunarSurfaceMapHeight,
                "default lunar surface map should use the expected equirectangular extent");
        require(default_map.mip_levels == 11U && default_map.mips.size() == default_map.mip_levels,
                "default lunar surface map should include a complete mip chain");
        cubey::procedural::validate_procedural_artifact_metadata(default_map.metadata);
        require(default_map.metadata.generator == "cubey::render::generate_lunar_surface_map",
                "lunar surface map metadata should identify its generator");
        require(default_map.metadata.formula_version == "lunar-surface-map-v15",
                "lunar surface map metadata should identify its formula version");
        require(default_map.metadata.domain == "render.lunar_surface_map",
                "lunar surface map metadata should identify its domain");
        require(default_map.metadata.space == cubey::procedural::ProceduralDomainSpace::Atlas,
                "lunar surface map metadata should use atlas domain space");
        require(default_map.metadata.kind == cubey::procedural::ProceduralArtifactKind::Texture2D,
                "lunar surface map metadata should identify a 2D texture");
        require(default_map.metadata.format ==
                    cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
                "lunar surface map metadata should identify RGBA8 payloads");
        require(default_map.metadata.extent.width == default_map.width &&
                    default_map.metadata.extent.height == default_map.height &&
                    default_map.metadata.extent.faces == 1U &&
                    default_map.metadata.extent.mip_levels == default_map.mip_levels,
                "lunar surface map metadata should preserve dimensions and mip count");
        require(default_map.metadata.content_hash == lunar_surface_map_hash(default_map.rgba8),
                "lunar surface map metadata hash should match map bytes");

        const LunarSurfaceMap map = generate_lunar_surface_map(128U, 64U);
        const LunarSurfaceMap map_again = generate_lunar_surface_map(128U, 64U);
        require(map.width == 128U && map.height == 64U && map.mip_levels == 8U,
                "small lunar surface map should preserve requested 2:1 dimensions");
        require(lunar_surface_map_hash(map.rgba8) == lunar_surface_map_hash(map_again.rgba8),
                "lunar surface map generation should be deterministic");
        require(map.metadata.seed == map_again.metadata.seed,
                "lunar surface map metadata seed should be deterministic");
        require(cubey::procedural::procedural_artifact_sample_count(map.metadata.extent) ==
                    map.rgba8.size() / 4U,
                "lunar surface map metadata sample count should match RGBA texels");
        for (std::uint32_t mip = 0; mip < map.mip_levels; ++mip) {
            const LunarSurfaceMapMip& level = map.mips.at(mip);
            require(level.width >= 1U && level.height >= 1U,
                    "lunar surface map mip dimensions should be nonzero");
            require(level.byte_offset + level.byte_count <= map.rgba8.size(),
                    "lunar surface map mip bytes should stay within the backing storage");
            require(level.byte_count == static_cast<std::size_t>(level.width) *
                                            static_cast<std::size_t>(level.height) * 4U,
                    "lunar surface map mips should be tightly packed RGBA8");
        }

        std::vector<std::uint8_t> base_albedo;
        base_albedo.reserve(static_cast<std::size_t>(map.width) *
                            static_cast<std::size_t>(map.height));
        for (std::uint32_t y = 0; y < map.height; ++y) {
            for (std::uint32_t x = 0; x < map.width; ++x) {
                base_albedo.push_back(lunar_surface_map_texel(map, x, y)[0]);
            }
        }
        std::ranges::sort(base_albedo);
        const std::uint8_t base_p10 = base_albedo[base_albedo.size() / 10U];
        const std::uint8_t base_p50 = base_albedo[base_albedo.size() / 2U];
        const std::uint8_t base_p90 = base_albedo[(base_albedo.size() * 9U) / 10U];

        std::uint32_t darker_region_count = 0U;
        std::uint32_t brighter_region_count = 0U;
        for (std::uint8_t value : base_albedo) {
            if (static_cast<int>(value) + 12 < static_cast<int>(base_p50)) {
                ++darker_region_count;
            }
            if (static_cast<int>(value) > static_cast<int>(base_p50) + 8) {
                ++brighter_region_count;
            }
        }

        const std::uint32_t broad_mip = std::min<std::uint32_t>(3U, map.mip_levels - 1U);
        const cubey::render::LunarSurfaceMapMip& broad_level = map.mips.at(broad_mip);
        std::vector<std::uint8_t> broad_albedo;
        broad_albedo.reserve(static_cast<std::size_t>(broad_level.width) *
                             static_cast<std::size_t>(broad_level.height));
        for (std::uint32_t y = 0; y < broad_level.height; ++y) {
            for (std::uint32_t x = 0; x < broad_level.width; ++x) {
                broad_albedo.push_back(lunar_surface_map_texel(map, x, y, broad_mip)[0]);
            }
        }
        std::ranges::sort(broad_albedo);
        const std::uint8_t broad_p10 = broad_albedo[broad_albedo.size() / 10U];
        const std::uint8_t broad_p90 = broad_albedo[(broad_albedo.size() * 9U) / 10U];

        const std::uint8_t* seam_left = lunar_surface_map_texel(map, 0U, map.height / 2U);
        const std::uint8_t* seam_right =
            lunar_surface_map_texel(map, map.width - 1U, map.height / 2U);
        const std::uint8_t* north = lunar_surface_map_texel(map, map.width / 2U, 0U);
        const std::uint8_t* center = lunar_surface_map_texel(map, map.width / 2U, map.height / 2U);
        require(static_cast<int>(base_p10) + 36 < static_cast<int>(base_p90),
                "lunar surface map should keep clear far-field albedo contrast");
        require(base_p10 > 45U && base_p90 < 225U,
                "lunar surface map should avoid crushed blacks and blown highlands");
        require(darker_region_count * 8U > base_albedo.size(),
                "lunar surface map should include enough broad dark plains");
        require(brighter_region_count * 8U > base_albedo.size(),
                "lunar surface map should include enough bright highland texture");
        require(static_cast<int>(broad_p10) + 18 < static_cast<int>(broad_p90),
                "lunar surface map broad albedo shapes should survive mip downsampling");
        require(std::abs(static_cast<int>(seam_left[0]) - static_cast<int>(seam_right[0])) < 24,
                "lunar surface map should stay continuous across the longitude seam");
        require(north[0] > 20U && north[0] < 230U,
                "lunar surface map poles should stay finite and populated");
        require(center[1] > 55U && center[1] < 200U && center[2] > 55U && center[2] < 200U,
                "lunar surface map packed normal detail should stay in a usable range");
        require(center[3] > 80U && center[3] <= 255U,
                "lunar surface map alpha should carry a roughness/detail mask");
    }

    {
        const NightSkyAtlasConfig procedural_config{
            .procedural_variation = 0.0F,
        };
        const NightSkyAtlas default_atlas = generate_night_sky_atlas(procedural_config);
        require(default_atlas.extent == kNightSkyAtlasExtent && default_atlas.mip_levels == 10U,
                "default night sky atlas should use a 512-face cubemap with mips");

        const NightSkyAtlas atlas = generate_night_sky_atlas(procedural_config, 64U);
        const NightSkyAtlas atlas_again = generate_night_sky_atlas(procedural_config, 64U);
        const NightSkyAtlas varied = generate_night_sky_atlas(
            NightSkyAtlasConfig{
                .procedural_variation = 3.0F,
            },
            64U);
        const NightSkyAtlas dust_layer = generate_night_sky_atlas(
            NightSkyAtlasConfig{
                .layer = NightSkyLayerView::DustTau,
            },
            64U);
        const NightSkyAtlas hii_layer = generate_night_sky_atlas(
            NightSkyAtlasConfig{
                .layer = NightSkyLayerView::HiiEmission,
            },
            64U);
        const NightSkyAtlas speckle_layer = generate_night_sky_atlas(
            NightSkyAtlasConfig{
                .layer = NightSkyLayerView::Speckles,
            },
            64U);
        require(atlas.extent == 64U && atlas.mip_levels == 7U,
                "night sky atlas should use the requested power-of-two extent");
        require(dust_layer.layer == NightSkyLayerView::DustTau &&
                    hii_layer.layer == NightSkyLayerView::HiiEmission &&
                    speckle_layer.layer == NightSkyLayerView::Speckles,
                "procedural night sky atlas should preserve selected diagnostic layer");
        require(atlas.mips.size() == atlas.mip_levels,
                "night sky atlas should include a complete mip chain");
        require(night_sky_atlas_hash(atlas.rgba32f) == night_sky_atlas_hash(atlas_again.rgba32f),
                "procedural night sky atlas generation should be deterministic");
        require(night_sky_atlas_hash(atlas.rgba32f) != night_sky_atlas_hash(varied.rgba32f),
                "procedural night sky atlas variation should alter generated structure");
        require(night_sky_atlas_hash(atlas.rgba32f) != night_sky_atlas_hash(dust_layer.rgba32f),
                "procedural diagnostic layers should differ from final output");
        cubey::procedural::validate_procedural_artifact_metadata(atlas.metadata);
        require(atlas.metadata.generator == "cubey::render::generate_night_sky_atlas",
                "night sky atlas metadata should identify its generator");
        require(atlas.metadata.formula_version == "atmosphere-night-sky-atlas-v1",
                "night sky atlas metadata should identify its formula version");
        require(atlas.metadata.domain == "atmosphere.night_sky_atlas",
                "night sky atlas metadata should identify its domain");
        require(atlas.metadata.seed == atlas_again.metadata.seed,
                "night sky atlas metadata seed should be deterministic");
        require(atlas.metadata.seed != varied.metadata.seed,
                "night sky atlas metadata seed should track procedural variation");
        require(atlas.metadata.space == cubey::procedural::ProceduralDomainSpace::Spherical,
                "night sky atlas metadata should use spherical domain space");
        require(atlas.metadata.kind == cubey::procedural::ProceduralArtifactKind::TextureCube,
                "night sky atlas metadata should identify a cube texture");
        require(atlas.metadata.format ==
                    cubey::procedural::ProceduralArtifactValueFormat::Rgba32Float,
                "night sky atlas metadata should identify RGBA32F payloads");
        require(atlas.metadata.extent.width == atlas.extent &&
                    atlas.metadata.extent.height == atlas.extent &&
                    atlas.metadata.extent.faces == 6U &&
                    atlas.metadata.extent.mip_levels == atlas.mip_levels,
                "night sky atlas metadata should preserve dimensions, faces, and mip count");
        require(atlas.metadata.content_hash == night_sky_atlas_hash(atlas.rgba32f),
                "night sky atlas metadata hash should match atlas floats");
        require(cubey::procedural::procedural_artifact_sample_count(atlas.metadata.extent) ==
                    atlas.rgba32f.size() / 4U,
                "night sky atlas metadata sample count should match RGBA texels");
        for (std::uint32_t mip = 0; mip < atlas.mip_levels; ++mip) {
            const NightSkyAtlasMip& level = atlas.mips.at(mip);
            require(level.extent >= 1U, "night sky atlas mip dimensions should be nonzero");
            require(level.byte_offset + level.byte_count <= atlas.rgba32f.size() * sizeof(float),
                    "night sky atlas mip bytes should stay within the backing storage");
            require(level.byte_count == static_cast<std::size_t>(level.extent) *
                                            static_cast<std::size_t>(level.extent) * 6U * 4U *
                                            sizeof(float),
                    "night sky atlas mips should be tightly packed RGBA32F cube faces");
        }

        const TestVec3 pole = test_normalize({0.31F, 0.84F, 0.44F});
        const TestVec3 center_hint = test_normalize({-0.45F, -0.12F, -0.89F});
        const TestVec3 center =
            test_normalize(test_sub(center_hint, test_mul(pole, test_dot(center_hint, pole))));
        const TestVec3 tangent = test_normalize(test_cross(pole, center));
        const float core_luma = night_sky_luminance_at(atlas, center);
        const float off_plane_luma = night_sky_luminance_at(atlas, pole);
        float dust_luma = std::numeric_limits<float>::max();
        float adjacent_luma = 0.0F;
        for (int index = 0; index < 9; ++index) {
            const float lane_latitude = -0.08F + static_cast<float>(index) * 0.0075F;
            const float adjacent_latitude = 0.03F + static_cast<float>(index) * 0.011F;
            dust_luma = std::min(
                dust_luma,
                night_sky_luminance_at(
                    atlas, test_normalize(test_sub(center, test_mul(pole, -lane_latitude)))));
            adjacent_luma = std::max(
                adjacent_luma,
                night_sky_luminance_at(
                    atlas, test_normalize(test_sub(center, test_mul(pole, -adjacent_latitude)))));
        }
        require(core_luma > off_plane_luma * 8.0F,
                "procedural night sky core should be brighter than off-plane sky");
        require(dust_luma < adjacent_luma * 0.95F,
                "procedural night sky dust lane should darken adjacent galactic light");
        const AtlasLuminanceStats dust_stats = night_sky_luminance_stats(dust_layer, 0.006F);
        const AtlasLuminanceStats hii_stats = night_sky_luminance_stats(hii_layer, 0.0004F);
        const AtlasLuminanceStats speckle_stats = night_sky_luminance_stats(speckle_layer, 0.0008F);
        require(dust_stats.max - dust_stats.min > 0.006F,
                "procedural dust optical depth layer should expose visible contrast");
        require(hii_stats.above_threshold > 0U &&
                    hii_stats.above_threshold < atlas.extent * atlas.extent * 6U / 3U,
                "procedural H II layer should be sparse and nonzero");
        require(speckle_stats.above_threshold > 0U,
                "procedural speckle layer should contain faint dense stars");
        const float seam_latitude = 0.035F;
        const float seam_epsilon = 0.025F;
        const float seam_a = night_sky_luminance_at(
            atlas,
            test_galactic_direction(center, tangent, pole, std::numbers::pi_v<float> - seam_epsilon,
                                    seam_latitude));
        const float seam_b = night_sky_luminance_at(
            atlas,
            test_galactic_direction(center, tangent, pole,
                                    -std::numbers::pi_v<float> + seam_epsilon, seam_latitude));
        require(std::abs(seam_a - seam_b) < std::max(seam_a, seam_b) * 0.35F + 0.0001F,
                "procedural night sky should stay continuous across the longitude seam");
        for (const float value : atlas.rgba32f) {
            require(std::isfinite(value) && value >= 0.0F,
                    "night sky atlas values should be finite and nonnegative");
        }
    }

    {
        TimeOfDayConfig solar_noon;
        solar_noon.time_hours = 12.0F;
        solar_noon.day_of_year = 80.0F;
        solar_noon.latitude_degrees = 30.0F;
        solar_noon.azimuth_offset_degrees = 0.0F;
        const SolarPosition position = atmosphere_solar_position(solar_noon);
        require_near(position.elevation_degrees, 60.0F, 0.2F,
                     "solar equinox noon at 30 degrees latitude should resolve near 60 degrees");
        require_near(position.azimuth_degrees, 0.0F, 0.2F,
                     "solar equinox noon should face scene south");
    }
    {
        TimeOfDayConfig morning;
        morning.time_hours = 9.0F;
        morning.azimuth_offset_degrees = 0.0F;
        TimeOfDayConfig afternoon = morning;
        afternoon.time_hours = 15.0F;
        require(atmosphere_solar_position(morning).azimuth_degrees > 0.0F,
                "morning solar azimuth should be east-positive");
        require(atmosphere_solar_position(afternoon).azimuth_degrees < 0.0F,
                "afternoon solar azimuth should be west-negative");
    }
    {
        require_near(atmosphere_wrap_signed_degrees(360.0F), 0.0F, 0.001F,
                     "azimuth wrapping should preserve equivalent full turns");
        TimeOfDayConfig morning;
        morning.time_hours = 9.0F;
        morning.day_of_year = 80.0F;
        morning.latitude_degrees = 30.0F;
        morning.azimuth_offset_degrees = 300.0F;
        const SolarPosition position = atmosphere_solar_position(morning);
        require_near(position.azimuth_degrees, 3.435F, 0.01F,
                     "solar azimuth offset should wrap instead of clamp");
    }
    {
        TimeOfDayConfig sunset;
        sunset.time_hours = 17.8F;
        sunset.azimuth_offset_degrees = 0.0F;
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
        TimeOfDayConfig lunar_time;
        lunar_time.time_hours = 0.0F;
        lunar_time.day_of_year = 80.0F;
        lunar_time.latitude_degrees = 30.0F;
        MoonConfig full_moon;
        full_moon.phase_offset_days = 14.765294F;
        const LunarState full_state = atmosphere_lunar_state(lunar_time, full_moon);
        require_near(full_state.phase_fraction, 0.5F, 0.001F,
                     "full moon phase fraction should be near half a lunar cycle");
        require_near(full_state.illumination, 1.0F, 0.001F,
                     "full moon illumination should be near one");
        require(full_state.direction.y > 0.4F,
                "full moon should be above the horizon near midnight for default latitude");

        MoonConfig new_moon;
        new_moon.phase_offset_days = 0.0F;
        const LunarState new_state = atmosphere_lunar_state(lunar_time, new_moon);
        require_near(new_state.phase_fraction, 0.0F, 0.001F,
                     "new moon phase fraction should start the lunar cycle");
        require_near(new_state.illumination, 0.0F, 0.001F,
                     "new moon illumination should be near zero");
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
        AtmosphereConfig invalid = defaults;
        invalid.moon.angular_radius_scale = 0.0F;
        require_throws([&invalid] { validate_atmosphere_config(invalid); },
                       "atmosphere config should reject invalid moon controls");
    }
    {
        AtmosphereConfig invalid = defaults;
        invalid.camera_pitch_offset_degrees = 95.0F;
        require_throws([&invalid] { validate_atmosphere_config(invalid); },
                       "atmosphere config should reject invalid camera view offsets");
    }
    {
        cubey::RunConfig run_config;
        run_config.atmosphere.preset = "sunset";
        run_config.debug_view = "moon";
        run_config.atmosphere.night_sky_mode = "camera";
        run_config.atmosphere.milky_way_layer = "dust-tau";
        run_config.atmosphere.sun_elevation_degrees = 6.0F;
        run_config.atmosphere.sun_azimuth_degrees = 33.0F;
        run_config.atmosphere.camera_altitude_km = 2.0F;
        run_config.atmosphere.camera_yaw_offset_degrees = 18.0F;
        run_config.atmosphere.camera_pitch_offset_degrees = -24.0F;
        run_config.atmosphere.mie_scale = 2.25F;
        run_config.atmosphere.twilight_strength = 1.50F;
        run_config.atmosphere.twilight_horizon_warmth = 0.80F;
        run_config.atmosphere.star_intensity = 1.70F;
        run_config.atmosphere.star_density = 0.40F;
        run_config.atmosphere.milky_way_intensity = 1.20F;
        run_config.atmosphere.milky_way_contrast = 1.80F;
        run_config.atmosphere.light_pollution = 0.25F;
        run_config.atmosphere.milky_way_variation = 2.0F;
        run_config.atmosphere.moon = 0;
        run_config.atmosphere.moon_intensity = 1.25F;
        run_config.atmosphere.moonlight_intensity = 1.50F;
        run_config.atmosphere.moon_phase_offset_days = 7.25F;
        run_config.atmosphere.moon_size_scale = 1.75F;
        run_config.atmosphere.reference_geometry = 0;
        run_config.atmosphere.ground_mode = "sky-only-no-ground-occlusion";
        AtmosphereConfig config = atmosphere_config_from_run_config(run_config);
        require(config.preset == AtmospherePreset::Sunset,
                "run config should select atmosphere preset");
        require(config.render_view == AtmosphereRenderView::Moon,
                "run config should select atmosphere debug view");
        require(config.night_sky.visual_mode == NightSkyVisualMode::Camera &&
                    config.night_sky.layer == NightSkyLayerView::DustTau,
                "run config should select night sky visual mode and layer");
        require(config.sun_elevation_degrees == 6.0F && config.sun_azimuth_degrees == 33.0F &&
                    config.camera_altitude_km == 2.0F && config.mie_density_scale == 2.25F,
                "run config atmosphere overrides should win over preset defaults");
        require(config.camera_yaw_offset_degrees == 18.0F &&
                    config.camera_pitch_offset_degrees == -24.0F,
                "run config atmosphere camera view offsets should map");
        require(config.night_sky.twilight_strength == 1.50F &&
                    config.night_sky.twilight_horizon_warmth == 0.80F &&
                    config.night_sky.star_intensity == 1.70F &&
                    config.night_sky.star_density == 0.40F &&
                    config.night_sky.milky_way_intensity == 1.20F &&
                    config.night_sky.milky_way_contrast == 1.80F &&
                    config.night_sky.light_pollution == 0.25F &&
                    config.night_sky.procedural_variation == 2.0F,
                "run config night sky overrides should win over preset defaults");
        require(!config.moon.enabled && config.moon.disk_intensity == 1.25F &&
                    config.moon.moonlight_intensity == 1.50F &&
                    config.moon.phase_offset_days == 7.25F &&
                    config.moon.angular_radius_scale == 1.75F,
                "run config moon overrides should win over preset defaults");
        require(!config.reference_geometry_enabled,
                "run config should disable atmosphere reference geometry");
        require(config.ground_mode ==
                    cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
                "run config should select atmosphere ground mode");
        require(atmosphere_environment_config(config).ground_mode ==
                    cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
                "atmosphere environment config should preserve selected ground mode");
        require(config.time_of_day.mode == SunControlMode::ManualSun,
                "manual sun overrides should force manual sun mode");
        require(config.time_of_day.auto_exposure_enabled,
                "manual sun mode should keep the default auto exposure");
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
    const std::filesystem::path repo_root = source_root.parent_path().parent_path();
    const std::string app_source = read_text_file(source_root / "atmosphere_app.cpp");
    const std::string environment_header = read_text_file(source_root / "atmosphere_environment.h");
    const std::string environment_source =
        read_text_file(source_root / "atmosphere_environment.cpp");
    const std::string shared_environment_header =
        read_text_file(repo_root / "include/cubey/render/atmosphere_environment.h");
    const std::string shared_environment_source =
        read_text_file(repo_root / "src/cubey/render/atmosphere_environment.cpp");
    const std::string shared_background_header =
        read_text_file(repo_root / "include/cubey/render/atmosphere_background_frame.h");
    const std::string shared_background_source =
        read_text_file(repo_root / "src/cubey/render/atmosphere_background_frame.cpp");
    const std::string shared_helper_source =
        read_text_file(repo_root / "shaders/cubey/atmosphere.glsl");
    const std::string shader_source =
        read_text_file(repo_root / "shaders/cubey/atmosphere/atmosphere.frag");
    const std::string celestial_shader_source =
        read_text_file(repo_root / "shaders/cubey/sky/celestial_body.frag");
    const std::string cloud_march_source =
        read_text_file(repo_root / "shaders/cubey/cloud/cloud_march.comp");
    const std::string surface_cloud_march_source =
        read_text_file(repo_root / "shaders/cubey/cloud/surface_cloud_march.comp");
    const std::string lunar_surface_source =
        read_text_file(repo_root / "src/cubey/render/lunar_surface_map.cpp");
    const std::string cmake_source = read_text_file(source_root / "CMakeLists.txt");
    const std::string render_cmake_source = read_text_file(repo_root / "src/cubey/CMakeLists.txt");
    require_contains(app_source, "GpuTimestampProfiler",
                     "atmosphere app should own GPU timestamp diagnostics");
    require_contains(app_source, ".gpu_timings = latest_gpu_timings()",
                     "atmosphere performance UI should show GPU pass timings");
    require_contains(app_source, ".profiler = profiler",
                     "atmosphere render graph should record GPU pass timings");
    require_contains(shared_environment_header, "struct AtmosphereEnvironmentFrameUniforms",
                     "shared atmosphere environment should define frame uniforms");
    require_contains(shared_environment_header,
                     "static_assert(sizeof(AtmosphereEnvironmentFrameUniforms)",
                     "shared atmosphere environment should lock frame uniform size");
    require_contains(shared_environment_header, "sizeof(float) * 72U",
                     "shared atmosphere environment should include celestial frame uniforms");
    require_contains(shared_environment_source, "atmosphere_environment_frame_uniforms",
                     "shared atmosphere environment should own frame uniform packing");
    require_contains(shared_environment_source, "atmosphere_environment_solar_position",
                     "shared atmosphere environment should own solar position math");
    require_contains(shared_environment_source, "atmosphere_environment_lunar_state",
                     "shared atmosphere environment should own lunar state math");
    require_contains(shared_environment_source, "atmosphere_environment_auto_exposure",
                     "shared atmosphere environment should own auto exposure math");
    require_contains(shared_environment_source, "smoothstep(-6.0F, 20.0F",
                     "shared atmosphere environment should own exposure transition shaping");
    require_contains(environment_source, "switch (view)",
                     "project atmosphere environment should map render views explicitly");
    require_contains(environment_source, "unknown atmosphere render view",
                     "project atmosphere environment should reject unknown render views");
    require_contains(environment_header, "using AtmosphereFrameUniforms",
                     "project atmosphere environment should alias shared frame uniforms");
    require_contains(environment_source, "atmosphere_environment_frame_uniforms",
                     "project atmosphere environment should delegate frame packing");
    require_contains(environment_header, "cubey/render/atmosphere_environment.h",
                     "project atmosphere environment should depend on the shared environment API");
    require_contains(environment_header, "using AtmosphereFrameUniforms",
                     "project atmosphere environment should expose shared frame uniform type");
    require_contains(
        environment_source, "atmosphere_environment_config(config)",
        "project atmosphere environment should adapt project config into shared config");
    require_contains(environment_header, "atmosphere_environment_render_view",
                     "project atmosphere environment should expose explicit render-view mapping");
    require_contains(environment_header, "atmosphere_environment_config",
                     "project atmosphere environment should expose shared config adaptation");
    require_contains(environment_header, "atmosphere_frame_uniforms",
                     "project atmosphere environment should expose frame uniform packing");
    require_contains(environment_header, "atmosphere_sun_direction",
                     "project atmosphere environment should expose shared sun direction");
    require_contains(environment_source, "atmosphere_environment_sun_direction",
                     "project atmosphere environment should delegate sun direction");
    require_contains(environment_source, "atmosphere_environment_frame_uniforms",
                     "project atmosphere environment should delegate frame uniforms");
    require_contains(shared_background_header, "class AtmosphereBackgroundFrame",
                     "shared render should expose the atmosphere background frame helper");
    require_contains(shared_background_header, "AtmosphereBackgroundTextureBindings",
                     "shared render should expose reusable atmosphere texture bindings");
    require_contains(shared_background_header, "lunar_surface_sampler",
                     "shared render should expose visible moon surface bindings");
    require_contains(shared_background_header, "FrameUniforms = 0",
                     "shared render should name atmosphere frame uniform binding zero");
    require_contains(shared_background_source, "atmosphere_background_pass_info",
                     "shared render should own atmosphere pass metadata");
    require_not_contains(shared_background_header, "atmosphere_lunar_atlas",
                         "shared render should not expose the removed lunar disk atlas");
    require_not_contains(shared_background_source, "AtmosphereBackgroundBinding::MoonAtlas",
                         "shared render should not bind the removed lunar disk atlas");
    require_contains(shared_background_source, "AtmosphereBackgroundBinding::NightSkyAtlas",
                     "shared render should own night sky atlas binding metadata");
    require_contains(shared_background_source, "create_lunar_surface_map_texture",
                     "shared render should upload the visible moon surface map");
    require_contains(
        shared_background_source, "generate_lunar_surface_map",
        "shared render generated textures should include the visible moon surface map");
    require_contains(shared_background_source, "FrameUniformMaterialInstanceConfig",
                     "shared render should own atmosphere frame descriptor creation");
    require_contains(shared_background_source, "MaterialDescriptorWriter",
                     "shared render should refresh atmosphere atlas descriptors");
    require_contains(shared_background_source, "record_fullscreen_pipeline_draw",
                     "shared render should own atmosphere fullscreen draw recording");
    require_contains(render_cmake_source, "render/atmosphere_background_frame.cpp",
                     "shared render build should compile the atmosphere background helper");
    require_contains(app_source, "atmosphere_frame_uniforms(",
                     "atmosphere app should consume the environment uniform packer");
    require_contains(app_source, "AtmosphereBackgroundFrame",
                     "atmosphere app should use the shared atmosphere background helper");
    require_contains(app_source, "atmosphere_background_.create_materials",
                     "atmosphere app should create background descriptors through shared render");
    require_contains(app_source, "atmosphere_background_.update_texture_bindings",
                     "atmosphere app should refresh atlas descriptors through shared render");
    require_contains(app_source, "atmosphere_background_.create_pipeline",
                     "atmosphere app should create the background pipeline through shared render");
    require_contains(app_source, "atmosphere_background_.record_pass",
                     "atmosphere app should record the background pass through shared render");
    require_contains(app_source, "atmosphere_background_pass_info",
                     "atmosphere app should annotate the graph with shared pass metadata");
    require_not_contains(app_source, "FrameUniformMaterialInstance<AtmosphereFrameUniforms>",
                         "atmosphere app should not own background frame descriptors directly");
    require_contains(app_source, "RenderGraphFrameExecutor",
                     "atmosphere app should record scene and post passes through the render graph");
    require_contains(app_source, "atmosphere scene color",
                     "atmosphere app should render into an HDR scene color target");
    require_contains(app_source, "kAtmosphereSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT",
                     "atmosphere scene color should use a floating point HDR format");
    require_contains(app_source, "HdrPostFrame",
                     "atmosphere app should use the shared HDR post frame helper");
    require_contains(app_source, "hdr_scene_color_texture_desc",
                     "atmosphere app should use the shared HDR scene color descriptor helper");
    require_contains(app_source, "forward_pbr_post.frag.spv",
                     "atmosphere app should load the shared PBR post shader");
    require_contains(app_source, ".external_background = true",
                     "atmosphere clouds should declare external background composition");
    require_contains(surface_cloud_march_source, "float cloud_shape_domain_m()",
                     "local cloud detail projection should use explicit domain scale");
    require_contains(surface_cloud_march_source, "params.density_options.y * 1000.0",
                     "local cloud domain scale should come from cloud uniforms");
    require_contains(cloud_march_source, "cloud_orbit_surface_sample",
                     "general cloud march shader should retain aerial/orbit diagnostics");
    require_contains(surface_cloud_march_source, "Lean surface-volume cloud path",
                     "surface cloud march shader should document the lean production path");
    require_contains(cloud_march_source, "!cloud_external_background_enabled()",
                     "external-background clouds should not force the standalone horizon assist");
    require_contains(cmake_source, "forward_pbr_post.frag",
                     "atmosphere build should compile the shared PBR post fragment shader");
    require_contains(cmake_source, "sky/celestial_body.vert",
                     "atmosphere build should compile the shared celestial body vertex shader");
    require_contains(cmake_source, "sky/celestial_body.frag",
                     "atmosphere build should compile the shared celestial body fragment shader");
    require_contains(app_source, "CelestialBodyFrame",
                     "atmosphere app should use the shared geometry moon frame");
    require_contains(app_source, "pending_lunar_surface_map_",
                     "atmosphere app should generate the visible moon surface separately");
    require_contains(app_source, "textures.lunar_surface_sampler",
                     "atmosphere visible moon geometry should bind the surface map");
    require_contains(app_source, "record_moon_body_frame",
                     "atmosphere app should record visible moon geometry outside the shader disk");
    require_contains(app_source, "CelestialBodyDepthMode::None",
                     "atmosphere moon geometry should render as a no-depth backdrop");
    require_contains(app_source, "config.render_moon_disk = false",
                     "atmosphere background should suppress the inline moon shader disk");
    require_contains(app_source, "background_render_view",
                     "atmosphere moon surface debug view should use a mesh-owned backdrop");
    require_contains(app_source, "render_view_ == AtmosphereRenderView::MoonSurface ? 0.0F",
                     "atmosphere moon surface debug view should use neutral post exposure");
    require_contains(app_source, "render_view_ == AtmosphereRenderView::Moon ||",
                     "atmosphere moon debug views should render geometry");
    require_contains(app_source, "render_view_ == AtmosphereRenderView::Moon",
                     "atmosphere moon debug view should use the geometry moon");
    require_contains(app_source, "const bool framed_moon_debug = moon_debug || surface_debug",
                     "atmosphere moon debug views should frame the moon toward camera");
    require_contains(app_source, "moon_debug ? camera_forward",
                     "atmosphere moon debug view should light the framed moon for material review");
    require_contains(app_source, "CelestialBodyShadingMode::SurfaceDebug",
                     "atmosphere moon surface debug view should use sphere surface diagnostics");
    require_contains(app_source, "moon.angular_radius_rad = surface_debug ? 0.34F",
                     "atmosphere moon surface debug view should render a centered close-up sphere");
    require_contains(
        celestial_shader_source, "textureLod(lunar_surface_map, uv, 0.0)",
        "moon surface debug should inspect base texture detail instead of averaged mips");
    require_contains(celestial_shader_source, "const vec3 sample_normal = -normal",
                     "moon surface sampling should face the generated near-side map toward camera");
    require_contains(celestial_shader_source, "-dot(sample_normal, basis_right)",
                     "moon surface sampling should keep nearside longitude orientation readable");
    require_contains(lunar_surface_source, "body-space broad mare field",
                     "lunar surface generation should use body-space procedural mare fields");
    require_contains(lunar_surface_source, "body-space mare warp x",
                     "lunar surface generation should domain-warp the body-space mare field");
    require_contains(lunar_surface_source, "mare_field_direction",
                     "lunar surface generation should orient mare fields in body space");
    require_contains(
        lunar_surface_source, "near_side_bias",
        "lunar surface generation should bias broad maria toward the generated nearside");
    require_contains(lunar_surface_source, "near_side_surface_direction",
                     "lunar surface generation should orient the generated nearside presentation");
    require_contains(lunar_surface_source, "central_basin_bias",
                     "lunar surface generation should keep the largest maria off the limb");
    require_contains(lunar_surface_source, "material_direction",
                     "lunar surface generation should perturb sphere-normal material sampling");
    require_contains(lunar_surface_source, "normal-space surface tone",
                     "lunar surface generation should use normal-space procedural tone");
    require_contains(lunar_surface_source, "subtle moon disk tone",
                     "lunar surface generation should layer subtle full-surface tone");
    require_not_contains(lunar_surface_source, "MariaPlain",
                         "lunar surface albedo should not use hand-authored maria stamps");
    require_not_contains(lunar_surface_source, "plain_mask",
                         "lunar surface albedo should not use primitive mare masks");
    require_contains(
        shader_source, "debug_view == CUBEY_ATMOSPHERE_VIEW_MOON ||",
        "atmosphere shader should leave mesh-owned moon debug views with a black backdrop");
    require_not_contains(app_source, "pending_lunar_atlas_",
                         "atmosphere app should not generate the removed lunar disk atlas");
    require_not_contains(app_source, "generate_lunar_atlas",
                         "atmosphere app should not upload the removed lunar disk atlas");
    require_not_contains(shader_source, "layout(push_constant)",
                         "atmosphere shader should not use push constants for frame data");
    require_not_contains(shader_source, "cubey_pbr_apply_display_transform",
                         "atmosphere shader should leave display transform to the post pass");
    require_not_contains(shader_source, "display_transform",
                         "atmosphere shader should not carry a stale display transform uniform");
    require_contains(shader_source, "layout(set = 0, binding = 0) uniform AtmosphereFrame",
                     "atmosphere shader should read frame data from a uniform buffer");
    require_contains(shader_source, "#include \"cubey/atmosphere.glsl\"",
                     "atmosphere shader should use the shared atmosphere helper include");
    require_contains(shader_source, "#include \"cubey/procedural/noise.glsl\"",
                     "atmosphere shader should use shared procedural noise helpers");
    require_contains(shader_source, "cubey_proc_hash_pcg_2d",
                     "atmosphere shader should use shared 2D PCG hashes for stars");
    require_contains(shader_source, "cubey_proc_value_noise_pcg_2d",
                     "atmosphere shader should use shared 2D value noise for night sky detail");
    require_not_contains(shader_source, "float hash12",
                         "atmosphere shader should not keep local 2D hash helpers");
    require_not_contains(shader_source, "float value_noise",
                         "atmosphere shader should not keep local value-noise helpers");
    require_contains(cmake_source, "shaders/cubey/procedural/noise.glsl",
                     "atmosphere build should track shared procedural noise dependency");
    require_contains(cmake_source, "shaders/cubey/procedural/random.glsl",
                     "atmosphere build should track shared procedural random dependency");
    require_contains(shared_helper_source, "cubey_atmosphere_rayleigh_phase",
                     "shared atmosphere include should define Rayleigh phase");
    require_contains(shared_helper_source, "cubey_atmosphere_mie_phase",
                     "shared atmosphere include should define Mie phase");
    require_contains(shared_helper_source, "cubey_atmosphere_classify_sky_background_ray",
                     "shared atmosphere include should define sky-background ray classification");
    require_contains(shader_source, "ray_sphere_intersection",
                     "atmosphere shader should intersect atmosphere and ground spheres");
    require_contains(shared_helper_source, "cubey_atmosphere_ozone_density",
                     "shared atmosphere include should define ozone absorption density");
    require_contains(shader_source, "transmittance_from_depth",
                     "atmosphere shader should expose transmittance");
    require_contains(shader_source, "sun_visibility",
                     "atmosphere shader should soften low-sun planet shadow visibility");
    require_contains(shader_source, "ground_sun_visibility",
                     "atmosphere shader should soften low-sun ground lighting");
    require_contains(shader_source, "ATMOSPHERE_MIN_TWILIGHT_SOFTNESS",
                     "atmosphere shader should include a twilight terminator softness floor");
    require_contains(shared_helper_source, "CUBEY_ATMOSPHERE_VIEW_AERIAL_PERSPECTIVE = 6",
                     "shared atmosphere shader include should define aerial debug view value");
    require_contains(shader_source, "debug_view == CUBEY_ATMOSPHERE_VIEW_AERIAL_PERSPECTIVE",
                     "atmosphere shader should include aerial perspective debug output");
    require_contains(shader_source, "twilight_radiance",
                     "atmosphere shader should include twilight radiance");
    require_contains(shader_source, "night_airglow_radiance",
                     "atmosphere shader should include a continuous night airglow fill");
    require_contains(shader_source, "safe_horizontal_direction",
                     "atmosphere shader should guard vertical twilight vectors");
    require_contains(shader_source, "procedural_star_radiance",
                     "atmosphere shader should include procedural stars");
    require_contains(shader_source, "bright_star_radiance",
                     "atmosphere shader should separate bright stars");
    require_contains(shader_source, "anchor_star_radiance",
                     "atmosphere shader should include sparse bright anchor stars");
    require_contains(shader_source, "naked_eye_star_radiance",
                     "atmosphere shader should include naked-eye star population");
    require_contains(shader_source, "faint_star_radiance",
                     "atmosphere shader should separate faint stars");
    require_contains(shader_source, "star_limiting_magnitude",
                     "atmosphere shader should fade stars by limiting magnitude");
    require_contains(shader_source, "star_magnitude_weight",
                     "atmosphere shader should weight stars by sampled magnitude");
    require_contains(shader_source, "night_object_visibility",
                     "atmosphere shader should share night object visibility");
    require_contains(shader_source, "star_sample_direction",
                     "atmosphere shader should rotate star sampling through celestial space");
    require_contains(shader_source, "celestial_options",
                     "atmosphere shader should receive celestial rotation options");
    require_contains(shared_helper_source, "CUBEY_ATMOSPHERE_VIEW_NIGHT_SKY = 7",
                     "shared atmosphere shader include should define night sky debug view value");
    require_contains(shader_source, "debug_view == CUBEY_ATMOSPHERE_VIEW_NIGHT_SKY",
                     "atmosphere shader should include night sky debug output");
    require_contains(shader_source,
                     "layout(set = 0, binding = 1) uniform samplerCube night_sky_atlas",
                     "atmosphere shader should sample a night sky atlas");
    require_contains(shader_source, "milky_way_radiance",
                     "atmosphere shader should include Milky Way radiance");
    require_contains(shared_helper_source, "CUBEY_ATMOSPHERE_VIEW_MILKY_WAY = 8",
                     "shared atmosphere shader include should define Milky Way debug view value");
    require_contains(shader_source, "debug_view == CUBEY_ATMOSPHERE_VIEW_MILKY_WAY",
                     "atmosphere shader should include Milky Way debug output");
    require_contains(shader_source, "galactic_debug_direction",
                     "atmosphere shader should map Milky Way debug view across the galactic plane");
    require_not_contains(shader_source, "moon_disk_radiance",
                         "atmosphere shader should not render the removed inline moon disk");
    require_not_contains(shader_source, "moon_atlas",
                         "atmosphere shader should not sample the removed lunar disk atlas");
    require_not_contains(shader_source, "moon_surface_sample",
                         "atmosphere shader should not keep lunar atlas surface sampling");
    require_not_contains(shader_source, "lunar_lambert",
                         "atmosphere shader should leave lunar lighting to geometry");
    require_contains(shared_helper_source, "CUBEY_ATMOSPHERE_VIEW_MOON = 9",
                     "shared atmosphere shader include should define moon debug view value");
    require_contains(shader_source, "debug_view == CUBEY_ATMOSPHERE_VIEW_MOON",
                     "atmosphere shader should include moon debug output");
    require_contains(shared_helper_source, "CUBEY_ATMOSPHERE_VIEW_MOON_SURFACE = 10",
                     "shared atmosphere shader include should define moon surface view value");
    require_contains(shader_source, "debug_view == CUBEY_ATMOSPHERE_VIEW_MOON_SURFACE",
                     "atmosphere shader should include moon surface debug output");
    require_contains(shader_source, "(hit_ground || !render_sun_disk) ? vec3(0.0)",
                     "atmosphere shader should mask sun disk behind ground or disabled content");
    require_contains(shader_source, "sun_halo_weight",
                     "atmosphere shader should include bounded sun halo weighting");
    require_contains(shader_source, "bool render_celestial_content = atmosphere.render_options.y",
                     "atmosphere shader should expose inline celestial content control");
    require_not_contains(shader_source, "bool render_moon_disk",
                         "atmosphere shader should not expose inline moon disk rendering control");
    require_not_contains(shader_source, "render_moon_surface_debug",
                         "atmosphere shader should not keep an enlarged moon atlas debug view");
    require_not_contains(shader_source, "moon_surface_debug_albedo",
                         "atmosphere shader should not keep moon atlas debug output");
    require_contains(shader_source, "ground_reference_geometry",
                     "atmosphere shader should include ground reference geometry");
    require_contains(shader_source, "reference_line",
                     "atmosphere shader should include antialiased reference lines");
}
