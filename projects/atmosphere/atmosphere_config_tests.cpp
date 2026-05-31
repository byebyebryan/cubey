#include "atmosphere_config.h"
#include "atmosphere_environment.h"
#include "lunar_atlas.h"
#include "night_sky_atlas.h"

#include <cubey/core/run_config.h>

#include <algorithm>
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

[[nodiscard]] const std::uint8_t*
lunar_atlas_texel(const cubey::projects::atmosphere::LunarAtlas& atlas, std::uint32_t x,
                  std::uint32_t y, std::uint32_t mip = 0) {
    const cubey::projects::atmosphere::LunarAtlasMip& level = atlas.mips.at(mip);
    const std::size_t texel =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(level.width) + x) * 4U;
    return atlas.rgba8.data() + level.byte_offset + texel;
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
    return test_normalize(test_add(
        test_add(test_mul(center, std::cos(longitude) * horizontal),
                 test_mul(tangent, std::sin(longitude) * horizontal)),
        test_mul(pole, std::sin(latitude))));
}

[[nodiscard]] float night_sky_luminance_at(
    const cubey::projects::atmosphere::NightSkyAtlas& atlas, TestVec3 direction) {
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
    const std::size_t offset =
        (static_cast<std::size_t>(face) * extent * extent + static_cast<std::size_t>(y) * extent +
         x) *
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

[[nodiscard]] AtlasLuminanceStats night_sky_luminance_stats(
    const cubey::projects::atmosphere::NightSkyAtlas& atlas, float threshold) {
    AtlasLuminanceStats stats;
    for (std::size_t index = 0; index + 2U < atlas.rgba32f.size(); index += 4U) {
        const float luma = atlas.rgba32f[index] * 0.2126F +
                           atlas.rgba32f[index + 1U] * 0.7152F +
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
    require(sizeof(AtmosphereFrameUniforms) == sizeof(float) * 64U,
            "atmosphere frame uniforms should keep the shader vec4 layout size");
    {
        AtmosphereConfig config = atmosphere_config_for_preset(AtmospherePreset::Noon);
        config.sun_elevation_degrees = 30.0F;
        config.sun_azimuth_degrees = 90.0F;
        const cubey::math::Vec3 sun = atmosphere_sun_direction(config);
        require_near(sun.x, std::cos(atmosphere_degrees_to_radians(30.0F)), 0.0001F,
                     "atmosphere sun direction should resolve azimuth around Y");
        require_near(sun.y, 0.5F, 0.0001F,
                     "atmosphere sun direction should resolve elevation");
        require(std::abs(sun.z) < 0.0001F,
                "atmosphere sun direction should face the requested azimuth");
    }
    {
        AtmosphereConfig config = atmosphere_config_for_preset(AtmospherePreset::MoonlitNight);
        const cubey::render::ViewRayBasis3D view_rays = cubey::render::view_ray_basis_3d(
            cubey::math::identity_quat(), 1.5F, std::numbers::pi_v<float> * 0.5F);
        const AtmosphereFrameUniforms uniforms = atmosphere_frame_uniforms(
            config,
            {
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
        require(uniforms.moon_options.x == 1.0F,
                "frame uniforms should pack the moon enable flag");
        require(uniforms.celestial_options.z ==
                    std::sin(atmosphere_degrees_to_radians(config.time_of_day.latitude_degrees)),
                "frame uniforms should pack the observer latitude sine");
    }
    {
        const AtmosphereConfig moonlit =
            atmosphere_config_for_preset(AtmospherePreset::MoonlitNight);
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
    require(defaults.reference_geometry_enabled && defaults.reference_grid_km > 0.0F &&
                defaults.reference_intensity > 0.0F,
            "default atmosphere config should expose reference ground geometry");
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

    {
        const LunarAtlas atlas = generate_lunar_atlas();
        const LunarAtlas atlas_again = generate_lunar_atlas();
        require(atlas.width == kLunarAtlasExtent && atlas.height == kLunarAtlasExtent,
                "lunar atlas should use the default square extent");
        require(atlas.mip_levels == 10U && atlas.mips.size() == atlas.mip_levels,
                "lunar atlas should include a complete mip chain");
        require(lunar_atlas_hash(atlas.rgba8) == lunar_atlas_hash(atlas_again.rgba8),
                "lunar atlas generation should be deterministic");
        for (std::uint32_t mip = 0; mip < atlas.mip_levels; ++mip) {
            const LunarAtlasMip& level = atlas.mips.at(mip);
            require(level.width >= 1U && level.height >= 1U,
                    "lunar atlas mip dimensions should be nonzero");
            require(level.byte_offset + level.byte_count <= atlas.rgba8.size(),
                    "lunar atlas mip bytes should stay within the backing storage");
            require(level.byte_count == static_cast<std::size_t>(level.width) *
                                            static_cast<std::size_t>(level.height) * 4U,
                    "lunar atlas mips should be tightly packed RGBA8");
        }

        const std::uint8_t* center = lunar_atlas_texel(atlas, 256U, 256U);
        const std::uint8_t* mare = lunar_atlas_texel(atlas, 146U, 270U);
        const std::uint8_t* highland = lunar_atlas_texel(atlas, 390U, 382U);
        require(center[0] >= 60U && center[0] <= 184U,
                "lunar atlas center albedo should stay in the expected lunar range");
        require(mare[0] < highland[0], "lunar atlas maria should be darker than highlands");
        require(center[1] > 55U && center[1] < 200U && center[2] > 55U && center[2] < 200U,
                "lunar atlas packed normals should stay in a usable range");
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
        require(night_sky_atlas_hash(atlas.rgba32f) ==
                    night_sky_atlas_hash(atlas_again.rgba32f),
                "procedural night sky atlas generation should be deterministic");
        require(night_sky_atlas_hash(atlas.rgba32f) != night_sky_atlas_hash(varied.rgba32f),
                "procedural night sky atlas variation should alter generated structure");
        require(night_sky_atlas_hash(atlas.rgba32f) !=
                    night_sky_atlas_hash(dust_layer.rgba32f),
                "procedural diagnostic layers should differ from final output");
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
        const AtlasLuminanceStats speckle_stats =
            night_sky_luminance_stats(speckle_layer, 0.0008F);
        require(dust_stats.max - dust_stats.min > 0.006F,
                "procedural dust optical depth layer should expose visible contrast");
        require(hii_stats.above_threshold > 0U && hii_stats.above_threshold < atlas.extent *
                    atlas.extent * 6U / 3U,
                "procedural H II layer should be sparse and nonzero");
        require(speckle_stats.above_threshold > 0U,
                "procedural speckle layer should contain faint dense stars");
        const float seam_latitude = 0.035F;
        const float seam_epsilon = 0.025F;
        const float seam_a = night_sky_luminance_at(
            atlas, test_galactic_direction(center, tangent, pole,
                                           std::numbers::pi_v<float> - seam_epsilon,
                                           seam_latitude));
        const float seam_b = night_sky_luminance_at(
            atlas, test_galactic_direction(center, tangent, pole,
                                           -std::numbers::pi_v<float> + seam_epsilon,
                                           seam_latitude));
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
        cubey::RunConfig run_config;
        run_config.atmosphere.preset = "sunset";
        run_config.debug_view = "moon";
        run_config.atmosphere.night_sky_mode = "camera";
        run_config.atmosphere.milky_way_layer = "dust-tau";
        run_config.atmosphere.sun_elevation_degrees = 6.0F;
        run_config.atmosphere.sun_azimuth_degrees = 33.0F;
        run_config.atmosphere.camera_altitude_km = 2.0F;
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
    const std::filesystem::path repo_root = source_root.parent_path().parent_path();
    const std::string app_source = read_text_file(source_root / "atmosphere_app.cpp");
    const std::string environment_header =
        read_text_file(source_root / "atmosphere_environment.h");
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
    const std::string shader_source = read_text_file(source_root / "shaders/atmosphere.frag");
    const std::string cmake_source = read_text_file(source_root / "CMakeLists.txt");
    const std::string render_cmake_source = read_text_file(repo_root / "src/cubey/CMakeLists.txt");
    require_contains(shared_environment_header, "struct AtmosphereEnvironmentFrameUniforms",
                     "shared atmosphere environment should define frame uniforms");
    require_contains(shared_environment_header,
                     "static_assert(sizeof(AtmosphereEnvironmentFrameUniforms)",
                     "shared atmosphere environment should lock frame uniform size");
    require_contains(shared_environment_header, "sizeof(float) * 64U",
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
    require_contains(environment_source, "atmosphere_environment_config(config)",
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
    require_contains(shared_background_header, "FrameUniforms = 0",
                     "shared render should name atmosphere frame uniform binding zero");
    require_contains(shared_background_source, "atmosphere_background_pass_info",
                     "shared render should own atmosphere pass metadata");
    require_contains(shared_background_source, "AtmosphereBackgroundBinding::MoonAtlas",
                     "shared render should own moon atlas binding metadata");
    require_contains(shared_background_source, "AtmosphereBackgroundBinding::NightSkyAtlas",
                     "shared render should own night sky atlas binding metadata");
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
    require_contains(cmake_source, "forward_pbr_post.frag",
                     "atmosphere build should compile the shared PBR post fragment shader");
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
    require_contains(shader_source, "debug_view == 7",
                     "atmosphere shader should include night sky debug output");
    require_contains(shader_source, "layout(set = 0, binding = 2) uniform samplerCube night_sky_atlas",
                     "atmosphere shader should sample a night sky atlas");
    require_contains(shader_source, "milky_way_radiance",
                     "atmosphere shader should include Milky Way radiance");
    require_contains(shader_source, "debug_view == 8",
                     "atmosphere shader should include Milky Way debug output");
    require_contains(shader_source, "galactic_debug_direction",
                     "atmosphere shader should map Milky Way debug view across the galactic plane");
    require_contains(shader_source, "moon_disk_radiance",
                     "atmosphere shader should include moon disk radiance");
    require_contains(shader_source, "layout(set = 0, binding = 1) uniform sampler2D moon_atlas",
                     "atmosphere shader should sample a generated lunar atlas");
    require_contains(shader_source, "moon_surface_sample",
                     "atmosphere shader should include lunar atlas surface sampling");
    require_contains(shader_source, "lunar_lambert",
                     "atmosphere shader should include lunar-style moon lighting");
    require_contains(shader_source, "debug_view == 9",
                     "atmosphere shader should include moon debug output");
    require_contains(shader_source, "debug_view == 10",
                     "atmosphere shader should include moon surface debug output");
    require_contains(shader_source, "hit_ground ? vec3(0.0) : sun_disk_luminance",
                     "atmosphere shader should mask sun disk behind ground");
    require_contains(shader_source, "render_moon_surface_debug",
                     "atmosphere shader should include an enlarged moon atlas debug view");
    require_contains(shader_source, "ground_reference_geometry",
                     "atmosphere shader should include ground reference geometry");
    require_contains(shader_source, "reference_line",
                     "atmosphere shader should include antialiased reference lines");
}
