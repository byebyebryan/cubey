#include "fluid_25d_builtin_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::builtin_detail {

void Fluid25DApp::draw_ui() {
    const bool was_flow_inspection_active = flow_inspection_active();
    const bool was_source_context = config_.hillside_source_context;
    const std::string was_hillside_camera = config_.hillside_camera;
    draw_fluid_25d_ui({
        .title = "Fluid 2.5D",
        .scenario = config_.simulation.scenario,
        .solver = config_.simulation.solver,
        .backend_metadata = &backend_metadata_,
        .transport_inspection_available =
            fluid_25d_transport_inspection_available(config_.simulation),
        .presentation_view = presentation_view_,
        .catchment_view = catchment_view_,
        .debug_view = debug_view_,
        .presentation_time_scale = config_.presentation_time_scale,
        .windowed_pacing = windowed_pacing_,
        .inspection_advance = inspection_advance_,
        .simulation_elapsed_seconds = source_schedule_.elapsed_seconds(config_.simulation),
        .fixed_delta_seconds = config_.simulation.fixed_delta_seconds,
        .continuous_source_m3_per_s = last_source_m3_per_s_,
        .hillside_supply = hillside_supply_,
        .rain_study = rain_study_,
        .rainfall_rate_mm_per_hour =
            config_.simulation.scenario == Fluid25DScenario::HillsideRainStudy
                ? rain_study_.applied_rate_mm_per_hour(config_.simulation)
                : 0.0,
        .rainfall_total_input_m3_per_s =
            config_.simulation.scenario == Fluid25DScenario::HillsideRainStudy
                ? rain_study_.total_input_m3_per_s(config_.simulation)
                : 0.0,
        .hillside_depth_cues = config_.catchment_render.hillside_depth_cues,
        .downstream_hillside_cameras_available =
            scenario_.width == 512U && scenario_.height == 512U,
        .hillside_source_context = config_.hillside_source_context,
        .hillside_camera = config_.hillside_camera,
        .resume_after_advance = resume_after_advance_,
        .motion_markers_available = config_.motion_markers,
        .local_motion_markers = config_.motion_marker_mode == "local",
        .show_motion_markers = show_motion_markers_,
        .dye_enabled = config_.simulation.dye_pulse_start_seconds.has_value(),
        .dye_start_seconds = config_.simulation.dye_pulse_start_seconds.value_or(0.0F),
        .dye_end_seconds = config_.simulation.dye_pulse_start_seconds.value_or(0.0F) +
                           config_.simulation.dye_pulse_duration_seconds.value_or(0.0F),
        .paused = paused_,
        .stopped = stopped_,
        .backend_failed = backend_metadata_.session.lifecycle == Fluid25DLifecycle::Failed,
        .reset_requested = reset_requested_,
        .presentation_cue_reset_requested = presentation_cue_reset_requested_,
        .quiver_reset_requested = quiver_reset_requested_,
    });
    if (was_source_context != config_.hillside_source_context ||
        was_hillside_camera != config_.hillside_camera) {
        configure_catchment_camera();
        orbit_controller_.reset();
    }
    if (!was_flow_inspection_active && flow_inspection_active()) {
        // Flow Inspection starts from a deterministic render-only field;
        // changing the reading mode never changes solver state.
        quiver_reset_requested_ = true;
    }
}

void Fluid25DApp::configure_catchment_camera() {
    const auto [terrain_minimum, terrain_maximum] = std::minmax_element(
        scenario_.terrain_height_m.begin(), scenario_.terrain_height_m.end());
    const float world_width =
        static_cast<float>(config_.simulation.grid_width - 1U) * config_.simulation.cell_size_m;
    const float world_height = static_cast<float>(config_.simulation.grid_height - 1U) *
                               config_.simulation.cell_size_m;
    const float horizontal_extent = std::max(world_width, world_height);
    const float render_height_scale = config_.catchment_render.terrain_height_scale.value_or(
        fluid_25d_catchment_height_scale(config_.simulation.scenario));
    const float default_camera_terrain_span =
        (*terrain_maximum - *terrain_minimum) *
        fluid_25d_catchment_height_scale(config_.simulation.scenario);
    float framing_horizontal_extent = horizontal_extent;
    catchment_target_ = {
        0.0F,
        (*terrain_minimum + *terrain_maximum) * 0.5F * render_height_scale,
        0.0F,
    };
    const bool authored_source_outlet =
        fluid_25d_is_source_outlet_demo(config_.simulation.scenario);
    const bool natural_flow_study =
        config_.simulation.scenario == Fluid25DScenario::NaturalFlowStudy;
    const bool rain_study = config_.simulation.scenario == Fluid25DScenario::HillsideRainStudy;
    const bool hillside = fluid_25d_is_macro_hillside_study(config_.simulation.scenario);
    const bool hillside_flow =
        config_.simulation.scenario == Fluid25DScenario::HillsideFlowStudy;
    const bool hillside_close =
        hillside_flow &&
        (config_.hillside_camera == "source" || config_.hillside_camera == "branch" ||
         config_.hillside_camera == "travel" || config_.hillside_camera == "collection" ||
         (config_.hillside_camera.empty() && config_.hillside_source_context));
    if (hillside_close) {
        framing_horizontal_extent =
            std::min(horizontal_extent, config_.hillside_camera == "source"       ? 1800.0F
                                        : config_.hillside_camera == "branch"     ? 2400.0F
                                        : config_.hillside_camera == "travel"     ? 2800.0F
                                        : config_.hillside_camera == "collection" ? 1600.0F
                                                                                  : 3200.0F);
        const std::uint32_t source_x =
            static_cast<std::uint32_t>(scenario_.source_cell % scenario_.width);
        const std::uint32_t source_z =
            static_cast<std::uint32_t>(scenario_.source_cell / scenario_.width);
        catchment_target_.x =
            (static_cast<float>(source_x) - 0.5F * static_cast<float>(scenario_.width - 1U)) *
            config_.simulation.cell_size_m;
        catchment_target_.z =
            (static_cast<float>(source_z) - 0.5F * static_cast<float>(scenario_.height - 1U)) *
            config_.simulation.cell_size_m;
        catchment_target_.y =
            scenario_.terrain_height_m[scenario_.source_cell] * render_height_scale;
        if (config_.hillside_camera == "branch") {
            // Framing only: the lower branch observed in the retained
            // study lies south-west of the source. No route or source
            // cell is changed by this presentation preset.
            const std::uint32_t branch_x = source_x > 12U ? source_x - 12U : 0U;
            const std::uint32_t branch_z = std::min(source_z + 30U, scenario_.height - 1U);
            catchment_target_.x = (static_cast<float>(branch_x) -
                                   0.5F * static_cast<float>(scenario_.width - 1U)) *
                                  config_.simulation.cell_size_m;
            catchment_target_.z = (static_cast<float>(branch_z) -
                                   0.5F * static_cast<float>(scenario_.height - 1U)) *
                                  config_.simulation.cell_size_m;
            catchment_target_.y =
                scenario_
                    .terrain_height_m[static_cast<std::size_t>(branch_z) * scenario_.width +
                                      branch_x] *
                render_height_scale;
        }
        if (config_.hillside_camera == "travel" || config_.hillside_camera == "collection") {
            // Frozen 512 reference at 7200 s: deepest collection beyond
            // 2 km is cell (58,251), relative to source (213,213). The
            // travel view covers the intervening western descent. These
            // are observation targets, not a prescribed hydraulic path.
            const int dx = config_.hillside_camera == "travel" ? -75 : -155;
            const int dz = config_.hillside_camera == "travel" ? 21 : 38;
            const auto x = static_cast<std::uint32_t>(std::clamp(
                static_cast<int>(source_x) + dx, 0, static_cast<int>(scenario_.width) - 1));
            const auto z = static_cast<std::uint32_t>(std::clamp(
                static_cast<int>(source_z) + dz, 0, static_cast<int>(scenario_.height) - 1));
            catchment_target_.x =
                (static_cast<float>(x) - 0.5F * static_cast<float>(scenario_.width - 1U)) *
                config_.simulation.cell_size_m;
            catchment_target_.z =
                (static_cast<float>(z) - 0.5F * static_cast<float>(scenario_.height - 1U)) *
                config_.simulation.cell_size_m;
            catchment_target_.y =
                scenario_.terrain_height_m[static_cast<std::size_t>(z) * scenario_.width + x] *
                render_height_scale;
        }
    }
    if (rain_study &&
        (config_.hillside_camera == "travel" || config_.hillside_camera == "collection")) {
        framing_horizontal_extent = std::min(
            horizontal_extent, config_.hillside_camera == "travel" ? 2800.0F : 1600.0F);
        const std::uint32_t x = config_.hillside_camera == "travel" ? 138U : 58U;
        const std::uint32_t z = config_.hillside_camera == "travel" ? 234U : 251U;
        const std::size_t cell = static_cast<std::size_t>(z) * scenario_.width + x;
        catchment_target_.x =
            (static_cast<float>(x) - 0.5F * static_cast<float>(scenario_.width - 1U)) *
            config_.simulation.cell_size_m;
        catchment_target_.z =
            (static_cast<float>(z) - 0.5F * static_cast<float>(scenario_.height - 1U)) *
            config_.simulation.cell_size_m;
        catchment_target_.y = scenario_.terrain_height_m[cell] * render_height_scale;
    }
    if (authored_source_outlet || natural_flow_study) {
        // Each explanation route gets a one-time endpoint framing with
        // modest terrain context. This changes neither later orbit/zoom
        // input nor any non-demonstration scenario.
        const auto world_x = [this](std::size_t index) {
            const std::uint32_t x = static_cast<std::uint32_t>(index % scenario_.width);
            return (static_cast<float>(x) - (0.5F * static_cast<float>(scenario_.width - 1U))) *
                   config_.simulation.cell_size_m;
        };
        const auto world_z = [this](std::size_t index) {
            const std::uint32_t y =
                static_cast<std::uint32_t>(index / static_cast<std::size_t>(scenario_.width));
            return (static_cast<float>(y) -
                    (0.5F * static_cast<float>(scenario_.height - 1U))) *
                   config_.simulation.cell_size_m;
        };
        const std::size_t endpoint_cell =
            natural_flow_study ? scenario_.outlet_cell : scenario_.sink_cell;
        const float route_span =
            std::max(std::abs(world_x(endpoint_cell) - world_x(scenario_.source_cell)),
                     std::abs(world_z(endpoint_cell) - world_z(scenario_.source_cell)));
        framing_horizontal_extent =
            std::max(32.0F, fluid_25d_catchment_home_horizontal_extent(
                                config_.simulation.scenario, horizontal_extent, route_span));
        catchment_target_.x = 0.5F * (world_x(scenario_.source_cell) + world_x(endpoint_cell));
        catchment_target_.z = 0.5F * (world_z(scenario_.source_cell) + world_z(endpoint_cell));
        if (config_.simulation.scenario == Fluid25DScenario::MountainSourceOutletDemo) {
            // The crop's high ridge is useful context but should not pull
            // the long low-elevation source-to-outlet route off screen.
            // This is a camera target only; imported elevations stay
            // untouched in every numerical buffer.
            catchment_target_.y = 0.5F *
                                  (scenario_.terrain_height_m[scenario_.source_cell] +
                                   scenario_.terrain_height_m[scenario_.sink_cell]) *
                                  render_height_scale;
        }
    }
    float camera_distance =
        std::max(framing_horizontal_extent *
                     fluid_25d_catchment_home_distance_scale(config_.simulation.scenario),
                 default_camera_terrain_span * (hillside ? 1.5F : 6.0F) + 16.0F);
    if (hillside && config_.hillside_camera == "travel")
        camera_distance = 3000.0F;
    if (hillside && config_.hillside_camera == "collection")
        camera_distance = 1200.0F;
    const auto [minimum_camera_distance, maximum_camera_distance] =
        fluid_25d_catchment_orbit_limits(config_.simulation.scenario, horizontal_extent,
                                         framing_horizontal_extent);
    if (config_.catchment_render.home_camera_distance_m.has_value()) {
        const float requested_distance = *config_.catchment_render.home_camera_distance_m;
        if (requested_distance < minimum_camera_distance ||
            requested_distance > maximum_camera_distance) {
            throw std::runtime_error("fluid 2.5D terrain home camera distance falls outside "
                                     "the current orbit limits");
        }
        camera_distance = requested_distance;
    }
    orbit_controller_.set_distance_limits(minimum_camera_distance, maximum_camera_distance);
    orbit_controller_.set_pitch_limits(-0.38F, 0.38F);
    orbit_controller_.set_home_distance(camera_distance);
    const float near_plane =
        std::max(kCatchmentCameraMinimumNearPlaneM,
                 framing_horizontal_extent * kCatchmentCameraNearExtentFraction);
    const float scaled_terrain_minimum_y = *terrain_minimum * render_height_scale;
    const float scaled_terrain_maximum_y = *terrain_maximum * render_height_scale;
    const cubey::math::Vec3 terrain_minimum_bounds{
        -0.5F * world_width,
        std::min(scaled_terrain_minimum_y, scaled_terrain_maximum_y),
        -0.5F * world_height,
    };
    const cubey::math::Vec3 terrain_maximum_bounds{
        0.5F * world_width,
        std::max(scaled_terrain_minimum_y, scaled_terrain_maximum_y),
        0.5F * world_height,
    };
    const float far_plane =
        fluid_25d_catchment_far_plane(maximum_camera_distance, terrain_minimum_bounds,
                                      terrain_maximum_bounds, catchment_target_);
    camera_.set_projection(fluid_25d_catchment_home_fovy_radians(
                               std::numbers::pi_v<float> / 3.0F, config_.simulation.scenario),
                           near_plane, far_plane);
}

[[nodiscard]] cubey::Transform3D Fluid25DApp::render_camera_transform() const {
    return cubey::orbit_camera_transform({
        .target = catchment_target_,
        .distance = orbit_controller_.distance(),
        .yaw = kCatchmentCameraBaseYaw + orbit_controller_.yaw(),
        .pitch = ((fluid_25d_is_natural_terrain_study(config_.simulation.scenario) ||
                   config_.simulation.scenario == Fluid25DScenario::HillsideRainStudy) &&
                          config_.natural_flow_home_pitch_radians.has_value()
                      ? *config_.natural_flow_home_pitch_radians
                      : fluid_25d_catchment_home_pitch(kCatchmentCameraBasePitch,
                                                       config_.simulation.scenario)) +
                 orbit_controller_.pitch(),
    });
}

[[nodiscard]] Fluid25DRenderCamera Fluid25DApp::render_camera(VkExtent2D extent) const {
    const cubey::Transform3D transform = render_camera_transform();
    const float aspect = extent.height == 0U ? 1.0F
                                             : static_cast<float>(extent.width) /
                                                   static_cast<float>(extent.height);
    return {
        .view_projection = camera_.view_projection_matrix(transform, aspect),
        .position = transform.translation,
    };
}

[[nodiscard]] bool Fluid25DApp::flow_inspection_active() const noexcept {
    return presentation_view_ == Fluid25DPresentationView::Catchment &&
           catchment_view_ == Fluid25DCatchmentView::FlowInspection;
}

} // namespace cubey::projects::fluid::fluid_25d::builtin_detail
