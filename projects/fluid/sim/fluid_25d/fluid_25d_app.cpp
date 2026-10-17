#include "fluid_25d_builtin_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::builtin_detail {

[[nodiscard]] Fluid25DScenarioData make_startup_scenario(Fluid25DProjectConfig& config) {
    validate_fluid_25d_project_config(config);
    const bool terrain_case = config.simulation.scenario == Fluid25DScenario::TerrainCase;
    const bool mountain_source_outlet =
        config.simulation.scenario == Fluid25DScenario::MountainSourceOutletDemo;
    const bool natural_flow_study = fluid_25d_is_natural_terrain_study(config.simulation.scenario);
    const bool rain_study = config.simulation.scenario == Fluid25DScenario::HillsideRainStudy;
    if (!terrain_case && !mountain_source_outlet && !natural_flow_study && !rain_study) {
        Fluid25DScenarioData scenario = make_fluid_25d_scenario(
            config.simulation.scenario, config.simulation.grid_width, config.simulation.grid_height,
            config.simulation.cell_size_m, config.simulation.headwaters_source_scale);
        if (!config.mass_audit_control.empty() && config.mass_audit_control != "none") {
            std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
            std::fill(scenario.source_depth_rate_m_per_s.begin(),
                      scenario.source_depth_rate_m_per_s.end(), 0.0F);
            std::fill(scenario.sink_depth_rate_m_per_s.begin(),
                      scenario.sink_depth_rate_m_per_s.end(), 0.0F);
            std::fill(scenario.boundary_outflow_face_mask.begin(),
                      scenario.boundary_outflow_face_mask.end(), 0U);
            scenario.source_cell = scenario.sink_cell = scenario.outlet_cell = kFluid25DNoCell;
            if (config.mass_audit_control == "source-basin") {
                std::fill(scenario.initial_water_depth_m.begin(),
                          scenario.initial_water_depth_m.end(), 1.0F);
                // Uniform supply isolates representable source additions;
                // without a surface gradient there is no horizontal transfer.
                std::fill(scenario.source_depth_rate_m_per_s.begin(),
                          scenario.source_depth_rate_m_per_s.end(), 20.0F / 900.0F);
            } else if (config.mass_audit_control == "dam-break") {
                std::fill(scenario.initial_water_depth_m.begin(),
                          scenario.initial_water_depth_m.end(), 0.02F);
                const std::uint32_t width = config.simulation.grid_width;
                const std::uint32_t height = config.simulation.grid_height;
                scenario.initial_water_depth_m[(height / 2U) * width + width / 2U] = 2.0F;
            } else {
                throw std::runtime_error("unknown fluid 2.5D mass audit control");
            }
            std::printf("fluid_25d: mass-audit-control=%s closed-boundary diagnostic fields\n",
                        config.mass_audit_control.c_str());
        }
        return scenario;
    }

    if (rain_study && config.rain_study_gpu_controls) {
        Fluid25DScenarioData scenario =
            make_fluid_25d_scenario(Fluid25DScenario::DryBed, config.simulation.grid_width,
                                    config.simulation.grid_height, config.simulation.cell_size_m);
        std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
        apply_fluid_25d_hillside_rain_study_forcing(config.simulation, scenario);
        std::fill(scenario.boundary_outflow_face_mask.begin(),
                  scenario.boundary_outflow_face_mask.end(), 0U);
        std::printf("fluid_25d: synthetic rain GPU controls use a flat closed native grid\n");
        return scenario;
    }

    cubey::asset::TerrainRasterHeightSource source(config.terrain.heightfield_path.value());
    const float source_spacing_m = source.sample_spacing_m();
    resolve_fluid_25d_terrain_cell_size(config, source_spacing_m);

    Fluid25DScenarioData scenario;
    if (terrain_case) {
        scenario = make_fluid_25d_terrain_scenario(config.simulation, source,
                                                   config.terrain.crop_x.value_or(0U),
                                                   config.terrain.crop_z.value_or(0U));
    } else if (mountain_source_outlet) {
        scenario = make_fluid_25d_mountain_source_outlet_scenario(config.simulation, source);
    } else if (rain_study) {
        scenario = make_fluid_25d_hillside_rain_study_scenario(config.simulation, source,
                                                               config.terrain.crop_x.value_or(0U),
                                                               config.terrain.crop_z.value_or(0U));
    } else {
        const Fluid25DNaturalFlowRecipe recipe =
            load_fluid_25d_natural_flow_recipe(config.natural_flow_recipe_path.value());
        validate_fluid_25d_natural_flow_recipe(recipe, config.simulation.grid_width,
                                               config.simulation.grid_height,
                                               config.simulation.cell_size_m);
        scenario = make_fluid_25d_natural_flow_study_scenario(config.simulation, source, recipe,
                                                              config.terrain.crop_x.value(),
                                                              config.terrain.crop_z.value());
    }
    if (!scenario.terrain_provenance.has_value()) {
        throw std::runtime_error("fluid 2.5D terrain-backed scenario did not produce provenance");
    }
    std::printf("fluid_25d: terrain-backed identity=%s source=%s manifest=%s\n",
                scenario.terrain_provenance->identity.c_str(),
                scenario.terrain_provenance->source_id.c_str(),
                scenario.terrain_provenance->manifest_path.string().c_str());
    return scenario;
}

Fluid25DApp::Fluid25DApp(Fluid25DProjectConfig config)
    : config_(std::move(config)),
      windowed_pacing_(config_.simulation.fixed_delta_seconds, config_.presentation_time_scale),
      scenario_(make_startup_scenario(config_)),
      presentation_view_(fluid_25d_presentation_view_from_name(config_.view)),
      catchment_view_(fluid_25d_catchment_view_from_name(config_.catchment_view)),
      debug_view_(fluid_25d_debug_view_from_name(config_.debug_view)) {
    if (config_.hillside_supply_gpu_controls) {
        // Independent synthetic control only, never the terrain study.
        scenario_.source_cell =
            (scenario_.height / 2U) * scenario_.width + scenario_.width / 2U;
        scenario_.source_depth_rate_m_per_s[scenario_.source_cell] = 0.04F;
        std::printf("fluid_25d: synthetic supply control scales 1 / 1.5 / 0.5 / 1\n");
    }
    backend_metadata_ = make_fluid_25d_builtin_backend_metadata(
        config_, scenario_, fluid_25d_new_backend_session_id());
    local_session_.emplace(backend_metadata_);
    backend_applied_time_scale_ = config_.presentation_time_scale;
    std::printf("fluid_25d_backend_startup_metadata: %s\n",
                encode_fluid_25d_backend_metadata_json(backend_metadata_).c_str());
    initial_water_volume_m3_ =
        fluid_25d_water_volume_m3(config_.simulation, scenario_.initial_water_depth_m);
    if (config_.simulation.scenario == Fluid25DScenario::TerrainCase) {
        std::printf(
            "fluid_25d: terrain-water protocol=%s rainfall_m_per_s=%.9f "
            "sheet_depth_m=%.6f\n",
            fluid_25d_terrain_water_protocol_name(config_.simulation.terrain_water_protocol),
            config_.simulation.rainfall_depth_rate_m_per_s,
            config_.simulation.sheet_initial_depth_m);
    }
    configure_catchment_camera();
    hillside_supply_ = Fluid25DHillsideSupply(config_.hillside_supply_response);
    last_source_m3_per_s_ = config_.simulation.natural_flow_source_m3_per_s;
    show_motion_markers_ = config_.motion_markers;
    if (config_.hillside_inspection_advance_seconds) {
        inspection_advance_.request(*config_.hillside_inspection_advance_seconds,
                                    config_.simulation.fixed_delta_seconds);
        paused_ = true;
    }
    if (config_.hillside_advance_and_continue_seconds) {
        inspection_advance_.request(*config_.hillside_advance_and_continue_seconds,
                                    config_.simulation.fixed_delta_seconds);
        resume_after_advance_ = true;
        paused_ = true;
    }
    if (config_.gpu_oracle_validation) {
        if (config_.simulation.solver == Fluid25DSolver::FiniteVolume) {
            finite_volume_oracle_.emplace(config_.simulation, scenario_);
        } else {
            oracle_.emplace(config_.simulation, scenario_);
        }
    }
}

int Fluid25DApp::run() {
    const int status = config_.common.headless ? run_headless() : run_windowed();
    if (local_session_->metadata().session.lifecycle == Fluid25DLifecycle::Failed)
        return 1;
    if (local_session_->metadata().session.lifecycle != Fluid25DLifecycle::Stopped)
        static_cast<void>(local_session_->publish(
            local_session_->metadata().session.physical_time_s, Fluid25DLifecycle::Completed));
    backend_metadata_ = local_session_->metadata();
    std::printf("fluid_25d_backend_runtime_metadata: %s\n",
                encode_fluid_25d_backend_metadata_json(backend_metadata_).c_str());
    return status;
}

int Fluid25DApp::run_windowed() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(),
                                          context.frame_slot_count());
    };
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        resources_.create_render_pipelines(context.device(), context.swapchain().format(),
                                           VK_FORMAT_D32_SFLOAT, context.swapchain().extent());
        if (config_.motion_markers)
            motion_markers_.create_render_pipeline(
                context.device(), context.swapchain().format(), VK_FORMAT_D32_SFLOAT,
                context.swapchain().extent());
        graph_executor_.clear();
        graph_executor_.resize(context.frame_slot_count());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
        graph_executor_.clear();
        motion_markers_.destroy_render_pipeline();
        resources_.destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context,
                              const FrameTiming& timing) {
        const bool was_flow_inspection_active = flow_inspection_active();
        const auto input = context.filtered_input();
        orbit_controller_.update_pointer_input(input, timing.delta_seconds);
        if (input.key_pressed(cubey::input::Key::Space) && !stopped_ &&
            backend_metadata_.session.lifecycle != Fluid25DLifecycle::Failed) {
            if (inspection_advance_.remaining_steps() > 0U) {
                inspection_advance_.reset();
                windowed_pacing_.reset();
                paused_ = true;
                resume_after_advance_ = false;
            } else {
                paused_ = !paused_;
            }
        }
        if (input.key_pressed(cubey::input::Key::R)) {
            stopped_ = false;
            reset_requested_ = true;
            presentation_cue_reset_requested_ = true;
            quiver_reset_requested_ = true;
            windowed_pacing_.reset();
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            debug_view_ = static_cast<Fluid25DDebugView>(
                (static_cast<std::uint32_t>(debug_view_) + 1U) % 6U);
        }
        if (input.key_pressed(cubey::input::Key::A)) {
            presentation_view_ = presentation_view_ == Fluid25DPresentationView::Diagnostics
                                     ? Fluid25DPresentationView::Catchment
                                     : Fluid25DPresentationView::Diagnostics;
        }
        if (!was_flow_inspection_active && flow_inspection_active()) {
            quiver_reset_requested_ = true;
        }
    };
    callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) { draw_ui(); };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_windowed_frame(context, frame);
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
        refresh_builtin_boundary(runtime_.gpu(), true);
        graph_executor_.clear();
        motion_markers_.destroy();
        resources_.destroy_all_resources();
        runtime_.detach_gpu_if_attached();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_.common,
            .app_name = "fluid_25d",
            .ready_status = "rendering River V0 terrain-water catchment",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int Fluid25DApp::run_headless() {
    if (!config_.gpu_oracle_validation) {
        std::printf(
            "fluid_25d: GPU oracle validation disabled; simulation state remains "
            "GPU-resident except finite-volume checks sticky status at final capture\n");
    }
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config_.common;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        const std::uint32_t frame_slot_count =
            cubey::host::headless_capture_frame_slot_count(config_.common);
        create_global_resources_if_needed(context.device(), context.gpu(), frame_slot_count);
        const cubey::host::HeadlessRenderTarget& target = context.render_target();
        resources_.create_render_pipelines(context.device(), target.format,
                                           VK_FORMAT_D32_SFLOAT, target.extent);
        if (config_.motion_markers)
            motion_markers_.create_render_pipeline(context.device(), target.format,
                                                   VK_FORMAT_D32_SFLOAT, target.extent);
        if (config_.motion_marker_gpu_controls)
            validate_fluid_25d_motion_marker_gpu_controls(context.device(), runtime_.gpu());
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
    };
    cubey::host::install_headless_simulation_driver(
        callbacks, config_.common,
        {
            .png_frame_count = headless_frame_count(config_.common),
            .png_timing =
                [this](std::uint64_t frame_index) {
                    return fixed_headless_timing(config_.simulation, frame_index);
                },
            .simulate_frame =
                [this](cubey::host::HeadlessPngContext& context,
                       const cubey::host::HeadlessCaptureFrame& frame) {
                    record_headless_simulation_frame(runtime_.gpu(), frame,
                                                     context.profile_recorder());
                },
        });
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                    const cubey::host::HeadlessCaptureFrame& frame,
                                    VkCommandBuffer command_buffer,
                                    const cubey::host::HeadlessRenderTarget& target) {
        refresh_builtin_boundary(runtime_.gpu(), true);
        validate_gpu_oracle(runtime_.gpu());
        const cubey::render::CompiledRenderGraph graph = build_fluid_25d_frame_graph(
            target, resources_, config_.simulation, presentation_view_, catchment_view_,
            debug_view_, render_camera(target.extent),
            Fluid25DRenderTargetMode::ColorAttachment, false, false, reset_requested_,
            presentation_cue_reset_requested_, quiver_reset_requested_, nullptr, 0U, {},
            config_.catchment_render,
            config_.motion_markers && show_motion_markers_ ? &motion_markers_ : nullptr, 1.0F);
        graph_executor_.record(
            {
                .device = &context.device(),
                .command_buffer = command_buffer,
                .frame_slot = frame.frame_slot,
                .label = "vkEndCommandBuffer fluid_25d headless graph",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            graph);
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext& context) {
        for (std::uint32_t slot = 0U; slot < motion_markers_.profile_slot_count(); ++slot)
            record_marker_timings(context.profile_recorder(), slot, true, false);
        graph_executor_.clear();
        motion_markers_.destroy();
        resources_.destroy_all_resources();
        runtime_.detach_gpu_if_attached();
    };
    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

} // namespace cubey::projects::fluid::fluid_25d::builtin_detail

namespace cubey::projects::fluid::fluid_25d {

int run_fluid_25d(const Fluid25DProjectConfig& config) {
    if (config.recording_path || config.stream_path || config.external_session_path)
        return run_fluid_25d_recording(config);
    builtin_detail::Fluid25DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid::fluid_25d
