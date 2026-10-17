#include "fluid_25d_builtin_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::builtin_detail {

void Fluid25DApp::record_headless_profile_diagnostics_if_requested(
    cubey::ProjectGpuServices& gpu, cubey::profiling::ProfileRecorder* profile_recorder,
    std::uint64_t frame_index) {
    if (!should_record_fluid_25d_profile_diagnostics(profile_recorder, config_.common,
                                                     frame_index)) {
        return;
    }
    const std::size_t cells = fluid_25d_cell_count(config_.simulation);
    const cubey::vulkan::Buffer& current_depth =
        resources_.current_depth_is_a() ? resources_.depth_a() : resources_.depth_b();
    const std::vector<float> depth_m =
        readback_values<float>(gpu, current_depth, cells, "profile diagnostic depth");
    const std::vector<Fluid25DVelocityGpu> velocity = readback_values<Fluid25DVelocityGpu>(
        gpu, resources_.velocity(), cells, "profile diagnostic velocity");
    if (config_.simulation.scenario == Fluid25DScenario::HillsideRainStudy) {
        const double physical_time_s =
            static_cast<double>(source_schedule_.completed_steps() + 1U) *
            config_.simulation.fixed_delta_seconds;
        const Fluid25DRainStudyObservation rain = compute_fluid_25d_rain_study_observation(
            config_.simulation, depth_m, velocity, rain_study_, physical_time_s);
        const auto record_rain = [&](const char* name, double value) {
            profile_recorder->record_metric(frame_index, "fluid_25d.rain", name, value);
        };
        record_rain("physical_time_s", rain.physical_time_s);
        record_rain("rate_mm_per_hour", rain.rate_mm_per_hour);
        record_rain("total_input_m3_per_s", rain.total_input_m3_per_s);
        record_rain("cumulative_depth_m", rain.cumulative_depth_m);
        record_rain("scheduled_volume_m3", rain.scheduled_volume_m3);
        record_rain("enabled", rain.enabled ? 1.0 : 0.0);
        record_rain("material_wet_cells", static_cast<double>(rain.material_wet_cells));
        record_rain("material_active_cells", static_cast<double>(rain.material_active_cells));
        record_rain("converged_water_volume_m3", rain.converged_water_volume_m3);
        record_rain("converged_moving_cells", static_cast<double>(rain.converged_moving_cells));
        record_rain("largest_corridor_cells", static_cast<double>(rain.largest_corridor_cells));
        record_rain("largest_corridor_span_m", rain.largest_corridor_span_m);
        record_rain("maximum_depth_m", rain.maximum_depth_m);
        record_rain("maximum_depth_cell_x", rain.maximum_depth_cell_x);
        record_rain("maximum_depth_cell_z", rain.maximum_depth_cell_z);
        for (const Fluid25DRainStudyRegionObservation& region : rain.regions) {
            const std::string category = "fluid_25d.rain.region." + std::string(region.name);
            profile_recorder->record_metric(frame_index, category, "valid",
                                            region.valid ? 1.0 : 0.0);
            profile_recorder->record_metric(frame_index, category, "water_volume_m3",
                                            region.water_volume_m3);
            profile_recorder->record_metric(frame_index, category, "direct_rain_volume_m3",
                                            region.direct_rain_volume_m3);
            profile_recorder->record_metric(frame_index, category, "net_lateral_storage_m3",
                                            region.net_lateral_storage_m3);
            profile_recorder->record_metric(frame_index, category, "maximum_depth_m",
                                            region.maximum_depth_m);
            profile_recorder->record_metric(frame_index, category, "material_wet_cells",
                                            static_cast<double>(region.material_wet_cells));
            profile_recorder->record_metric(frame_index, category, "active_cells",
                                            static_cast<double>(region.active_cells));
        }
    }
    if (config_.simulation.scenario == Fluid25DScenario::HillsideFlowStudy) {
        // Observation only: locate the intermediate moving footprint and
        // deepest downstream collection. Nothing feeds back to forcing.
        const double sx = static_cast<double>(scenario_.source_cell % scenario_.width);
        const double sz = static_cast<double>(scenario_.source_cell / scenario_.width);
        double sum = 0.0, weighted_x = 0.0, weighted_z = 0.0, deepest = 0.0;
        std::size_t collection_cell = scenario_.source_cell;
        for (std::size_t cell = 0U; cell < cells; ++cell) {
            if (depth_m[cell] <= 0.01F)
                continue;
            const double x = static_cast<double>(cell % scenario_.width);
            const double z = static_cast<double>(cell / scenario_.width);
            const double distance = std::hypot(x - sx, z - sz) * config_.simulation.cell_size_m;
            if (distance >= 600.0 && distance < 2000.0) {
                sum += depth_m[cell];
                weighted_x += depth_m[cell] * x;
                weighted_z += depth_m[cell] * z;
            }
            if (distance >= 2000.0 && depth_m[cell] > deepest) {
                deepest = depth_m[cell];
                collection_cell = cell;
            }
        }
        const auto observation = [&](const char* name, double value) {
            profile_recorder->record_metric(frame_index, "fluid_25d.hillside_observation", name,
                                            value);
        };
        observation("travel_cell_x", sum > 0.0 ? weighted_x / sum : sx);
        observation("travel_cell_z", sum > 0.0 ? weighted_z / sum : sz);
        observation("collection_cell_x",
                    static_cast<double>(collection_cell % scenario_.width));
        observation("collection_cell_z",
                    static_cast<double>(collection_cell / scenario_.width));
        observation("collection_depth_m", deepest);
    }
    const std::vector<Fluid25DLedgerGpu> ledger = readback_values<Fluid25DLedgerGpu>(
        gpu, resources_.ledger(), cells, "profile diagnostic ledger");
    const cubey::vulkan::Buffer& current_tracer_q =
        resources_.current_depth_is_a() ? resources_.tracer_q_a() : resources_.tracer_q_b();
    const std::vector<float> tracer_q_m =
        readback_values<float>(gpu, current_tracer_q, cells, "profile diagnostic tracer q");
    const std::vector<Fluid25DTracerLedgerGpu> tracer_ledger =
        readback_values<Fluid25DTracerLedgerGpu>(gpu, resources_.tracer_ledger(), cells,
                                                 "profile diagnostic tracer ledger");
    if (config_.simulation.solver == Fluid25DSolver::FiniteVolume) {
        const std::uint32_t status_flags = read_finite_volume_status(gpu, "profile diagnostic");
        profile_recorder->record_metric(frame_index, "fluid_25d.solver",
                                        "finite_volume_status_flags",
                                        static_cast<double>(status_flags));
        // Diagnostic-only identity of the complete hydraulic state. Dye
        // fields are deliberately excluded so pulse/no-pulse replays can
        // prove that the transport experiment did not change hydraulics.
        const auto momentum = readback_values<Fluid25DMomentumGpu>(
            gpu,
            resources_.current_depth_is_a() ? resources_.momentum_a() : resources_.momentum_b(),
            cells, "profile hydraulic identity momentum");
        const auto residual = readback_values<Fluid25DConservationResidualGpu>(
            gpu, resources_.finite_volume_conservation_residual(), cells,
            "profile hydraulic identity residual");
        std::uint64_t hash = 14695981039346656037ULL;
        const auto append = [&](float value) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned shift = 0; shift < 32; shift += 8) {
                hash ^= (bits >> shift) & 0xffU;
                hash *= 1099511628211ULL;
            }
        };
        for (std::size_t i = 0; i < cells; ++i) {
            append(depth_m[i]);
            for (float value : momentum[i].momentum_xy_reserved) {
                append(value);
            }
            for (float value : velocity[i].velocity_wet) {
                append(value);
            }
            for (float value : ledger[i].source_sink_boundary_reserved_m3) {
                append(value);
            }
            append(residual[i].depth_tracer_reserved[0]);
            for (float value : residual[i].water_ledger) {
                append(value);
            }
        }
        profile_recorder->record_metric(frame_index, "fluid_25d.hydraulic_identity",
                                        "hash_hi_u32", static_cast<double>(hash >> 32));
        profile_recorder->record_metric(frame_index, "fluid_25d.hydraulic_identity",
                                        "hash_lo_u32",
                                        static_cast<double>(hash & 0xffffffffULL));
    }
    const Fluid25DProfileDiagnostics diagnostics = compute_fluid_25d_profile_diagnostics(
        config_.simulation, depth_m, velocity, ledger, initial_water_volume_m3_);
    record_fluid_25d_profile_diagnostics(*profile_recorder, frame_index, diagnostics);
    if (config_.simulation.scenario == Fluid25DScenario::HillsideFlowStudy) {
        record_fluid_25d_hillside_progress(
            *profile_recorder, frame_index,
            compute_fluid_25d_hillside_progress(config_.simulation, scenario_, depth_m));
        record_fluid_25d_hillside_spatial(*profile_recorder, frame_index,
                                          compute_fluid_25d_hillside_spatial(config_.simulation,
                                                                             scenario_, depth_m,
                                                                             velocity));
        if (config_.motion_markers) {
            const auto markers = readback_values<Fluid25DMotionMarkerGpu>(
                gpu, motion_markers_.buffer(), kFluid25DMotionMarkerCount,
                "profile motion marker state");
            const float sx = static_cast<float>(scenario_.source_cell % scenario_.width);
            const float sy = static_cast<float>(scenario_.source_cell / scenario_.width);
            double active = 0.0, far = 0.0, total_distance = 0.0, max_age = 0.0;
            std::uint64_t hash = 14695981039346656037ULL;
            const auto append = [&](float value) {
                if (!std::isfinite(value))
                    throw std::runtime_error("nonfinite motion marker state");
                const auto bits = std::bit_cast<std::uint32_t>(value);
                for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                    hash ^= (bits >> shift) & 0xffU;
                    hash *= 1099511628211ULL;
                }
            };
            for (const auto& marker : markers) {
                for (float value : marker.position_age_active)
                    append(value);
                for (float value : marker.previous_xy_reserved)
                    append(value);
                for (const auto& point : marker.history)
                    for (float value : point)
                        append(value);
                const auto& p = marker.position_age_active;
                if (p[3] != 0.0F && p[3] != 1.0F)
                    throw std::runtime_error("invalid motion marker active flag");
                if (p[3] < 0.5F)
                    continue;
                const int x = static_cast<int>(std::floor(p[0] + 0.5F));
                const int y = static_cast<int>(std::floor(p[1] + 0.5F));
                if (x < 0 || y < 0 || x >= static_cast<int>(scenario_.width) ||
                    y >= static_cast<int>(scenario_.height) ||
                    depth_m[static_cast<std::size_t>(y) * scenario_.width +
                            static_cast<std::size_t>(x)] <=
                        config_.simulation.minimum_wet_depth_m)
                    throw std::runtime_error("motion marker lacks wet hydraulic support");
                const double distance =
                    std::hypot(p[0] - sx, p[1] - sy) * config_.simulation.cell_size_m;
                ++active;
                far = std::max(far, distance);
                total_distance += distance;
                max_age = std::max(max_age, static_cast<double>(p[2]));
            }
            const auto metric = [&](const char* name, double value) {
                profile_recorder->record_metric(frame_index, "fluid_25d.motion_markers", name,
                                                value);
            };
            metric("active_count", active);
            metric("farthest_from_source_m", far);
            metric("mean_from_source_m", active > 0.0 ? total_distance / active : 0.0);
            metric("maximum_age_s", max_age);
            metric("hash_hi_u32", static_cast<double>(hash >> 32U));
            metric("hash_lo_u32", static_cast<double>(hash & 0xffffffffULL));
        }
    }
    if ((config_.simulation.scenario == Fluid25DScenario::TerrainCase &&
         config_.simulation.terrain_water_protocol ==
             Fluid25DTerrainWaterProtocol::RainPulse) ||
        config_.simulation.scenario == Fluid25DScenario::SustainedHeadwatersDemo ||
        config_.simulation.scenario == Fluid25DScenario::HillsideRainStudy ||
        fluid_25d_is_natural_terrain_study(config_.simulation.scenario)) {
        record_fluid_25d_boundary_outflow_diagnostics(*profile_recorder, frame_index,
                                                      diagnostics);
    }
    if (config_.simulation.scenario == Fluid25DScenario::NaturalFlowStudy) {
        if (!scenario_.natural_flow_study.has_value()) {
            throw std::runtime_error(
                "fluid 2.5D natural-flow-study profile has no validated recipe metadata");
        }
        const Fluid25DNaturalFlowStudyMetadata& study = scenario_.natural_flow_study.value();
        const auto gauges = compute_fluid_25d_natural_flow_gauge_diagnostics(
            config_.simulation, study, depth_m, velocity, tracer_q_m);
        record_fluid_25d_natural_flow_gauge_diagnostics(*profile_recorder, frame_index, gauges);
        const Fluid25DNaturalFlowBoundaryLedgerDiagnostics boundary =
            compute_fluid_25d_natural_flow_boundary_ledger_diagnostics(
                config_.simulation, study, ledger,
                diagnostics.cumulative_boundary_outflow_volume_m3);
        record_fluid_25d_natural_flow_boundary_ledger_diagnostics(*profile_recorder,
                                                                  frame_index, boundary);
    }
    if (config_.simulation.scenario == Fluid25DScenario::SustainedHeadwatersDemo) {
        const auto stations = compute_fluid_25d_sustained_headwaters_stations(
            config_.simulation, depth_m, velocity);
        record_fluid_25d_sustained_headwaters_stations(*profile_recorder, frame_index,
                                                       stations);
        const auto cross_section_stations =
            fluid_25d_sustained_headwaters_cross_section_stations(config_.simulation);
        for (const Fluid25DSustainedHeadwatersCrossSectionStation& station :
             cross_section_stations) {
            const Fluid25DSustainedHeadwatersCrossSectionDiagnostics section =
                compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                    config_.simulation, station, scenario_.terrain_height_m, depth_m, velocity);
            record_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                *profile_recorder, frame_index, section);
        }
        const Fluid25DSustainedHeadwatersCorridorDiagnostics corridor =
            compute_fluid_25d_sustained_headwaters_corridor_diagnostics(config_.simulation,
                                                                        depth_m);
        record_fluid_25d_sustained_headwaters_corridor_diagnostics(*profile_recorder,
                                                                   frame_index, corridor);
    }
    const Fluid25DTracerProfileDiagnostics tracer_diagnostics =
        compute_fluid_25d_tracer_profile_diagnostics(config_.simulation, depth_m, tracer_q_m,
                                                     scenario_.sink_depth_rate_m_per_s,
                                                     tracer_ledger);
    record_fluid_25d_tracer_profile_diagnostics(*profile_recorder, frame_index,
                                                tracer_diagnostics);
    if (config_.simulation.mass_audit) {
        const auto audit_cells = readback_values<Fluid25DMassAuditGpu>(
            gpu, resources_.finite_volume_mass_audit(), cells, "committed mass audit");
        const auto water_audit = compute_fluid_25d_mass_audit(
            audit_cells, diagnostics.total_water_volume_m3, initial_water_volume_m3_,
            diagnostics.cumulative_source_volume_m3, diagnostics.cumulative_sink_volume_m3,
            diagnostics.cumulative_boundary_outflow_volume_m3);
        const auto tracer_audit = compute_fluid_25d_mass_audit(
            audit_cells, tracer_diagnostics.total_tracer_amount_m3, 0.0,
            tracer_diagnostics.cumulative_source_amount_m3,
            tracer_diagnostics.cumulative_sink_amount_m3,
            tracer_diagnostics.cumulative_boundary_outflow_amount_m3, true);
        const auto record_audit = [&](const Fluid25DMassAuditDiagnostics& audit,
                                      std::string_view category) {
            constexpr std::array<std::string_view, 9> terms{
                "independent_source_m3",   "independent_sink_m3",
                "independent_boundary_m3", "actual_source_addition_m3",
                "actual_sink_removal_m3",  "internal_transport_m3",
                "boundary_transport_m3",   "update_arithmetic_m3",
                "clamp_correction_m3"};
            for (std::size_t i = 0; i < terms.size(); ++i) {
                profile_recorder->record_metric(frame_index, category, terms[i],
                                                audit.totals_m3[i]);
            }
            profile_recorder->record_metric(frame_index, category, "field_budget_residual_m3",
                                            audit.field_budget_residual_m3);
            profile_recorder->record_metric(frame_index, category,
                                            "cumulative_ledger_rounding_m3",
                                            audit.cumulative_ledger_rounding_m3);
            profile_recorder->record_metric(frame_index, category,
                                            "source_representation_difference_m3",
                                            audit.source_representation_difference_m3);
            profile_recorder->record_metric(frame_index, category,
                                            "sink_representation_difference_m3",
                                            audit.sink_representation_difference_m3);
            profile_recorder->record_metric(frame_index, category,
                                            "boundary_definition_difference_m3",
                                            audit.boundary_definition_difference_m3);
            profile_recorder->record_metric(frame_index, category,
                                            "observed_conservation_residual_m3",
                                            audit.observed_conservation_residual_m3);
            profile_recorder->record_metric(frame_index, category, "committed_substeps",
                                            static_cast<double>(audit.committed_substeps));
        };
        record_audit(water_audit, "fluid_25d.mass_audit.water");
        record_audit(tracer_audit, "fluid_25d.mass_audit.tracer");
    }
    if (config_.simulation.scenario == Fluid25DScenario::SourceOutletDemo) {
        const auto stations = fluid_25d_source_outlet_cross_section_stations(
            config_.simulation.grid_width, config_.simulation.grid_height);
        for (const Fluid25DSourceOutletCrossSectionStation& station : stations) {
            const Fluid25DSourceOutletCrossSectionDiagnostics section =
                compute_fluid_25d_source_outlet_cross_section_diagnostics(
                    config_.simulation, station, scenario_.terrain_height_m, depth_m, velocity);
            record_fluid_25d_source_outlet_cross_section_diagnostics(*profile_recorder,
                                                                     frame_index, section);
        }
        const Fluid25DSourceOutletSpatialDiagnostics spatial =
            compute_fluid_25d_source_outlet_spatial_diagnostics(
                config_.simulation, scenario_.terrain_height_m, depth_m, tracer_q_m);
        record_fluid_25d_source_outlet_spatial_diagnostics(*profile_recorder, frame_index,
                                                           spatial);
    }
}

[[nodiscard]] std::uint32_t Fluid25DApp::read_finite_volume_status(cubey::ProjectGpuServices& gpu,
                                                                   const char* context) const {
    const std::vector<Fluid25DFiniteVolumeStatusGpu> status =
        readback_values<Fluid25DFiniteVolumeStatusGpu>(gpu, resources_.finite_volume_status(),
                                                       1U, context);
    const Fluid25DFiniteVolumeStatusGpu& value = status.front();
    if (value.flags_reserved[3] != 0U) {
        throw std::runtime_error("fluid 2.5D finite-volume GPU status padding is nonzero");
    }
    if (value.flags_reserved[0] != 0U) {
        throw std::runtime_error("fluid 2.5D finite-volume GPU status is nonzero: flags=" +
                                 std::to_string(value.flags_reserved[0]));
    }
    return value.flags_reserved[0];
}

} // namespace cubey::projects::fluid::fluid_25d::builtin_detail
