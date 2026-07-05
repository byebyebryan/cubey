#include <cubey/render/cloud_layer_config.h>

#include <cmath>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

cubey::render::CloudLayerViewRegimeInput regime_input(float altitude_m,
                                                      cubey::math::Vec3 forward) {
    constexpr float kRadius = 6371000.0F;
    return {
        .camera_position = {0.0F, kRadius + altitude_m, 0.0F},
        .camera_forward = forward,
        .planet_radius_m = kRadius,
        .orbit_transition_start_m = 45000.0F,
        .orbit_transition_end_m = 180000.0F,
    };
}

} // namespace

void test_cloud_layer_view_regime_resolves_surface_camera() {
    const cubey::render::CloudLayerViewRegime regime =
        cubey::render::cloud_layer_view_regime(regime_input(150.0F, {0.0F, 0.0F, -1.0F}));

    require_near(regime.camera_mode, 0.0F, 0.001F,
                 "surface cloud camera should use local mode");
    require_near(regime.altitude_m, 150.0F, 0.5F,
                 "surface cloud camera should report altitude");
    require(regime.horizon_grazing > 0.95F,
            "level surface cloud view should report grazing horizon factor");
}

void test_cloud_layer_view_regime_resolves_high_transition_camera() {
    const cubey::render::CloudLayerViewRegime regime =
        cubey::render::cloud_layer_view_regime(regime_input(90000.0F, {0.0F, -1.0F, 0.0F}));

    require_near(regime.camera_mode, 1.0F, 0.001F,
                 "high cloud camera should use transition mode");
    require(regime.altitude_blend > 0.2F && regime.altitude_blend < 0.5F,
            "high cloud camera should be inside the transition band");
}

void test_cloud_layer_view_regime_resolves_orbit_camera() {
    const cubey::render::CloudLayerViewRegime regime =
        cubey::render::cloud_layer_view_regime(regime_input(240000.0F, {0.0F, -1.0F, 0.0F}));

    require_near(regime.camera_mode, 4.0F, 0.001F,
                 "orbit cloud camera should enable full shell mode");
    require_near(regime.altitude_blend, 1.0F, 0.001F,
                 "orbit cloud camera should be beyond the transition band");
}

void test_cloud_layer_view_regime_promotes_grazing_high_camera() {
    const cubey::render::CloudLayerViewRegime regime =
        cubey::render::cloud_layer_view_regime(regime_input(12000.0F, {1.0F, 0.0F, 0.0F}));

    require_near(regime.camera_mode, 1.0F, 0.001F,
                 "grazing cloud camera should use transition mode for far shell support");
    require(regime.horizon_grazing > 0.95F,
            "grazing cloud camera should report strong horizon factor");
}

void test_cloud_layer_edge_mask_debug_view_round_trips() {
    const cubey::render::CloudLayerDebugView view =
        cubey::render::cloud_layer_debug_view_from_name("edge-mask");

    require(view == cubey::render::CloudLayerDebugView::EdgeMask,
            "cloud debug parser should recognize edge-mask");
    require(cubey::render::cloud_layer_debug_view_name(view) == std::string_view{"edge-mask"},
            "cloud debug names should round-trip edge-mask");
}

void test_cloud_layer_frame_uniforms_pack_environment_lighting() {
    cubey::render::CloudLayerConfig config{};
    cubey::render::CloudLayerFrameInfo frame{};
    frame.sun_direction = {0.0F, 0.8F, 0.6F};
    frame.sun_color = {1.0F, 0.9F, 0.7F};
    frame.sun_intensity = 0.35F;
    frame.moon_direction = {0.2F, 0.7F, -0.1F};
    frame.moon_color = {0.5F, 0.6F, 0.8F};
    frame.moon_intensity = 0.12F;
    frame.ambient_color = {0.04F, 0.05F, 0.07F};
    frame.ambient_intensity = 1.4F;
    frame.target_extent = {1280U, 720U};

    const cubey::render::CloudLayerFrameUniforms uniforms =
        cubey::render::cloud_layer_frame_uniforms(config, frame);

    require_near(uniforms.sun_direction_intensity.w, 0.35F, 0.001F,
                 "cloud uniforms should pack resolved sun intensity");
    require_near(uniforms.sun_color.y, 0.9F, 0.001F,
                 "cloud uniforms should pack resolved sun color");
    require_near(uniforms.moon_direction_intensity.x, 0.2F, 0.001F,
                 "cloud uniforms should pack moon direction");
    require_near(uniforms.moon_direction_intensity.w, 0.12F, 0.001F,
                 "cloud uniforms should pack moon intensity separately from sun");
    require_near(uniforms.moon_color.z, 0.8F, 0.001F,
                 "cloud uniforms should pack moon color");
    require_near(uniforms.ambient_color_intensity.z, 0.07F, 0.001F,
                 "cloud uniforms should pack environment ambient color");
    require_near(uniforms.ambient_color_intensity.w, 1.4F, 0.001F,
                 "cloud uniforms should pack environment ambient intensity");
}
