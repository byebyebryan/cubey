#include "fluid_25d_builtin_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::builtin_detail {

void Fluid25DApp::validate_gpu_oracle(cubey::ProjectGpuServices& gpu) {
    if (!oracle_.has_value() && !finite_volume_oracle_.has_value()) {
        // Ordinary headless finite-volume capture still surfaces a sticky
        // device failure at its one final capture boundary. Windowed use
        // deliberately remains GPU-resident and therefore cannot report
        // the flag to the host without an explicit diagnostic mode.
        if (config_.simulation.solver == Fluid25DSolver::FiniteVolume) {
            static_cast<void>(read_finite_volume_status(gpu, "headless final status"));
        }
        return;
    }
    const std::size_t cells = fluid_25d_cell_count(config_.simulation);
    const cubey::vulkan::Buffer& current_depth =
        resources_.current_depth_is_a() ? resources_.depth_a() : resources_.depth_b();
    const std::vector<float> actual_depth =
        readback_values<float>(gpu, current_depth, cells, "oracle depth");
    const std::vector<Fluid25DLedgerGpu> actual_ledger =
        readback_values<Fluid25DLedgerGpu>(gpu, resources_.ledger(), cells, "oracle ledger");
    const std::vector<Fluid25DVelocityGpu> actual_velocity =
        readback_values<Fluid25DVelocityGpu>(gpu, resources_.velocity(), cells,
                                             "oracle velocity");

    float maximum_depth_error = 0.0F;
    float maximum_velocity_error = 0.0F;
    float maximum_flux_error = 0.0F;
    float maximum_momentum_error = 0.0F;
    double actual_source_volume_m3 = 0.0;
    double actual_sink_volume_m3 = 0.0;
    double actual_boundary_outflow_volume_m3 = 0.0;
    for (std::size_t index = 0; index < cells; ++index) {
        if (!finite(actual_depth[index]) || actual_depth[index] < 0.0F) {
            throw std::runtime_error(
                "fluid 2.5D GPU oracle observed nonfinite or negative depth");
        }
        for (const float component : actual_velocity[index].velocity_wet) {
            if (!finite(component)) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed nonfinite velocity state");
            }
        }
        if (actual_velocity[index].velocity_wet[2] != 0.0F &&
            actual_velocity[index].velocity_wet[2] != 1.0F) {
            throw std::runtime_error("fluid 2.5D GPU oracle observed invalid wet-state bit");
        }
        actual_source_volume_m3 += actual_ledger[index].source_sink_boundary_reserved_m3[0];
        actual_sink_volume_m3 += actual_ledger[index].source_sink_boundary_reserved_m3[1];
        actual_boundary_outflow_volume_m3 +=
            actual_ledger[index].source_sink_boundary_reserved_m3[2];
        for (const float component : actual_ledger[index].source_sink_boundary_reserved_m3) {
            if (!finite(component)) {
                throw std::runtime_error("fluid 2.5D GPU oracle observed nonfinite ledger");
            }
        }
        if (actual_ledger[index].source_sink_boundary_reserved_m3[0] < 0.0F ||
            actual_ledger[index].source_sink_boundary_reserved_m3[1] < 0.0F ||
            actual_ledger[index].source_sink_boundary_reserved_m3[2] < 0.0F ||
            actual_ledger[index].source_sink_boundary_reserved_m3[3] != 0.0F) {
            throw std::runtime_error("fluid 2.5D GPU oracle observed invalid ledger state");
        }
    }
    const double source_ledger_error =
        std::abs(actual_source_volume_m3 - expected_source_volume_m3_);
    const double sink_ledger_error = std::abs(actual_sink_volume_m3 - expected_sink_volume_m3_);
    const double boundary_ledger_error =
        std::abs(actual_boundary_outflow_volume_m3 - expected_boundary_outflow_volume_m3_);
    const double source_ledger_tolerance_m3 =
        cumulative_ledger_tolerance_m3(expected_source_volume_m3_);
    const double sink_ledger_tolerance_m3 =
        cumulative_ledger_tolerance_m3(expected_sink_volume_m3_);
    const double boundary_ledger_tolerance_m3 =
        cumulative_ledger_tolerance_m3(expected_boundary_outflow_volume_m3_);
    if (oracle_.has_value()) {
        const std::vector<Fluid25DFluxGpu> actual_flux =
            readback_values<Fluid25DFluxGpu>(gpu, resources_.flux(), cells, "oracle flux");
        for (std::size_t index = 0; index < cells; ++index) {
            maximum_depth_error =
                std::max(maximum_depth_error,
                         std::abs(actual_depth[index] - oracle_->water_depth_m()[index]));
            for (std::size_t component = 0; component < 2U; ++component) {
                const float actual = actual_velocity[index].velocity_wet[component];
                const float expected = component == 0U
                                           ? oracle_->velocity_m_per_s()[index].x_m_per_s
                                           : oracle_->velocity_m_per_s()[index].y_m_per_s;
                maximum_velocity_error =
                    std::max(maximum_velocity_error, std::abs(actual - expected));
            }
            const float expected_wet = oracle_->wet_mask()[index] == 0U ? 0.0F : 1.0F;
            maximum_velocity_error =
                std::max(maximum_velocity_error,
                         std::abs(actual_velocity[index].velocity_wet[2] - expected_wet));
            for (std::size_t face = 0; face < 4U; ++face) {
                const float actual = actual_flux[index].faces_m3_per_s[face];
                if (!finite(actual) || actual < 0.0F) {
                    throw std::runtime_error(
                        "fluid 2.5D GPU oracle observed invalid face flux");
                }
                maximum_flux_error =
                    std::max(maximum_flux_error,
                             std::abs(actual - oracle_->outgoing_flux_m3_per_s()[index][face]));
            }
        }
        if (maximum_depth_error > kDepthToleranceM ||
            maximum_flux_error > kFluxToleranceM3PerS ||
            maximum_velocity_error > kVelocityToleranceMPerS ||
            source_ledger_error > source_ledger_tolerance_m3 ||
            sink_ledger_error > sink_ledger_tolerance_m3 ||
            boundary_ledger_error > boundary_ledger_tolerance_m3) {
            throw std::runtime_error(
                "fluid 2.5D GPU oracle mismatch: max_depth=" +
                std::to_string(maximum_depth_error) +
                " max_flux=" + std::to_string(maximum_flux_error) +
                " max_velocity=" + std::to_string(maximum_velocity_error) +
                " source_ledger=" + std::to_string(source_ledger_error) +
                " sink_ledger=" + std::to_string(sink_ledger_error) +
                " boundary_ledger=" + std::to_string(boundary_ledger_error));
        }
        std::printf("fluid_25d_gpu_oracle: PASS solver=virtual-pipes scenario=%s "
                    "max_depth=%.7f max_flux=%.7f max_velocity=%.7f source_ledger=%.7f "
                    "sink_ledger=%.7f boundary_ledger=%.7f\n",
                    fluid_25d_scenario_name(config_.simulation.scenario), maximum_depth_error,
                    maximum_flux_error, maximum_velocity_error, source_ledger_error,
                    sink_ledger_error, boundary_ledger_error);
        return;
    }

    const std::uint32_t status_flags = read_finite_volume_status(gpu, "oracle status");
    double actual_water_volume_m3 = 0.0;
    const double oracle_cell_area_m2 = static_cast<double>(config_.simulation.cell_size_m) *
                                       static_cast<double>(config_.simulation.cell_size_m);
    for (float h : actual_depth) {
        actual_water_volume_m3 += static_cast<double>(h) * oracle_cell_area_m2;
    }
    const double cpu_water_volume_m3 = finite_volume_oracle_->total_water_volume_m3();
    std::printf("fluid_25d_oracle_water_budget: gpu_stored=%.9f cpu_stored=%.9f "
                "gpu_residual=%.9f cpu_residual=%.9f\n",
                actual_water_volume_m3, cpu_water_volume_m3,
                actual_water_volume_m3 - initial_water_volume_m3_ - actual_source_volume_m3 +
                    actual_sink_volume_m3 + actual_boundary_outflow_volume_m3,
                cpu_water_volume_m3 - initial_water_volume_m3_ - expected_source_volume_m3_ +
                    expected_sink_volume_m3_ + expected_boundary_outflow_volume_m3_);
    const std::vector<Fluid25DMomentumGpu> actual_momentum =
        readback_values<Fluid25DMomentumGpu>(
            gpu,
            resources_.current_depth_is_a() ? resources_.momentum_a() : resources_.momentum_b(),
            cells, "oracle finite-volume momentum");
    const std::vector<float> actual_tracer_q = readback_values<float>(
        gpu,
        resources_.current_depth_is_a() ? resources_.tracer_q_a() : resources_.tracer_q_b(),
        cells, "oracle finite-volume tracer q");
    const std::vector<Fluid25DTracerLedgerGpu> actual_tracer_ledger =
        readback_values<Fluid25DTracerLedgerGpu>(gpu, resources_.tracer_ledger(), cells,
                                                 "oracle finite-volume tracer ledger");
    float maximum_tracer_q_error = 0.0F;
    float maximum_tracer_concentration_error = 0.0F;
    std::size_t maximum_velocity_error_cell = 0U;
    std::size_t maximum_velocity_error_component = 0U;
    float maximum_velocity_actual = 0.0F;
    float maximum_velocity_expected = 0.0F;
    double actual_tracer_amount_m3 = 0.0;
    double actual_tracer_source_amount_m3 = 0.0;
    double actual_tracer_sink_amount_m3 = 0.0;
    double actual_tracer_boundary_outflow_amount_m3 = 0.0;
    const double cell_area_m2 = static_cast<double>(config_.simulation.cell_size_m) *
                                static_cast<double>(config_.simulation.cell_size_m);
    for (std::size_t index = 0; index < cells; ++index) {
        maximum_depth_error = std::max(
            maximum_depth_error,
            std::abs(actual_depth[index] - finite_volume_oracle_->water_depth_m()[index]));
        const Fluid25DMomentum expected_momentum =
            finite_volume_oracle_->momentum_m2_per_s()[index];
        for (std::size_t component = 0; component < 2U; ++component) {
            const float actual = actual_momentum[index].momentum_xy_reserved[component];
            const float expected =
                component == 0U ? expected_momentum.x_m2_per_s : expected_momentum.y_m2_per_s;
            if (!finite(actual)) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed invalid finite-volume momentum");
            }
            maximum_momentum_error =
                std::max(maximum_momentum_error, std::abs(actual - expected));
            const float expected_velocity =
                component == 0U ? finite_volume_oracle_->velocity_m_per_s()[index].x_m_per_s
                                : finite_volume_oracle_->velocity_m_per_s()[index].y_m_per_s;
            const float actual_component = actual_velocity[index].velocity_wet[component];
            const float velocity_error = std::abs(actual_component - expected_velocity);
            if (velocity_error > maximum_velocity_error) {
                maximum_velocity_error = velocity_error;
                maximum_velocity_error_cell = index;
                maximum_velocity_error_component = component;
                maximum_velocity_actual = actual_component;
                maximum_velocity_expected = expected_velocity;
            }
        }
        if (actual_momentum[index].momentum_xy_reserved[2] != 0.0F ||
            actual_momentum[index].momentum_xy_reserved[3] != 0.0F) {
            throw std::runtime_error(
                "fluid 2.5D GPU oracle observed nonzero finite-volume padding");
        }
        const float expected_wet = finite_volume_oracle_->wet_mask()[index] == 0U ? 0.0F : 1.0F;
        const float wet_error = std::abs(actual_velocity[index].velocity_wet[2] - expected_wet);
        if (wet_error > maximum_velocity_error) {
            maximum_velocity_error = wet_error;
            maximum_velocity_error_cell = index;
            maximum_velocity_error_component = 2U;
            maximum_velocity_actual = actual_velocity[index].velocity_wet[2];
            maximum_velocity_expected = expected_wet;
        }
        const float actual_q = actual_tracer_q[index];
        const float expected_q = finite_volume_oracle_->tracer_mass_per_area_m()[index];
        if (!finite(actual_q) || actual_q < -1.0e-7F ||
            actual_q > actual_depth[index] + 1.0e-7F) {
            throw std::runtime_error(
                "fluid 2.5D GPU oracle observed invalid finite-volume tracer q");
        }
        maximum_tracer_q_error =
            std::max(maximum_tracer_q_error, std::abs(actual_q - expected_q));
        if (actual_depth[index] > config_.simulation.minimum_wet_depth_m &&
            finite_volume_oracle_->water_depth_m()[index] >
                config_.simulation.minimum_wet_depth_m) {
            const float actual_concentration = actual_q / actual_depth[index];
            const float expected_concentration =
                finite_volume_oracle_->tracer_concentration()[index];
            maximum_tracer_concentration_error =
                std::max(maximum_tracer_concentration_error,
                         std::abs(actual_concentration - expected_concentration));
        }
        actual_tracer_amount_m3 += static_cast<double>(std::max(0.0F, actual_q)) * cell_area_m2;
        const Fluid25DTracerLedgerGpu& tracer_ledger = actual_tracer_ledger[index];
        for (std::size_t component_index = 0U;
             component_index < tracer_ledger.source_sink_boundary_reserved_m3.size();
             ++component_index) {
            const float component =
                tracer_ledger.source_sink_boundary_reserved_m3[component_index];
            if (!finite(component)) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed nonfinite tracer ledger at cell=" +
                    std::to_string(index) + " component=" + std::to_string(component_index));
            }
        }
        if (tracer_ledger.source_sink_boundary_reserved_m3[0] < 0.0F ||
            tracer_ledger.source_sink_boundary_reserved_m3[1] < 0.0F ||
            tracer_ledger.source_sink_boundary_reserved_m3[2] < 0.0F ||
            tracer_ledger.source_sink_boundary_reserved_m3[3] != 0.0F) {
            throw std::runtime_error(
                "fluid 2.5D GPU oracle observed invalid tracer ledger at cell=" +
                std::to_string(index) +
                " values=" + std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[0]) +
                "," + std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[1]) + "," +
                std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[2]) + "," +
                std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[3]));
        }
        actual_tracer_source_amount_m3 += tracer_ledger.source_sink_boundary_reserved_m3[0];
        actual_tracer_sink_amount_m3 += tracer_ledger.source_sink_boundary_reserved_m3[1];
        actual_tracer_boundary_outflow_amount_m3 +=
            tracer_ledger.source_sink_boundary_reserved_m3[2];
    }
    const double tracer_source_ledger_error =
        std::abs(actual_tracer_source_amount_m3 - expected_tracer_source_amount_m3_);
    const double tracer_sink_ledger_error =
        std::abs(actual_tracer_sink_amount_m3 - expected_tracer_sink_amount_m3_);
    const double tracer_boundary_ledger_error = std::abs(
        actual_tracer_boundary_outflow_amount_m3 - expected_tracer_boundary_outflow_amount_m3_);
    const double tracer_conservation_residual_m3 =
        actual_tracer_amount_m3 - actual_tracer_source_amount_m3 +
        actual_tracer_sink_amount_m3 + actual_tracer_boundary_outflow_amount_m3;
    const double expected_tracer_amount_m3 = finite_volume_oracle_->total_tracer_amount_m3();
    const double tracer_amount_error =
        std::abs(actual_tracer_amount_m3 - expected_tracer_amount_m3);
    const double tracer_source_ledger_tolerance_m3 =
        cumulative_ledger_tolerance_m3(expected_tracer_source_amount_m3_);
    const double tracer_sink_ledger_tolerance_m3 =
        cumulative_ledger_tolerance_m3(expected_tracer_sink_amount_m3_);
    const double tracer_boundary_ledger_tolerance_m3 =
        cumulative_ledger_tolerance_m3(expected_tracer_boundary_outflow_amount_m3_);
    const double tracer_amount_tolerance_m3 =
        cumulative_ledger_tolerance_m3(expected_tracer_amount_m3);
    const double tracer_conservation_tolerance_m3 =
        cumulative_ledger_tolerance_m3(actual_tracer_amount_m3);
    if (status_flags != 0U || maximum_depth_error > kDepthToleranceM ||
        maximum_momentum_error > kMomentumToleranceM2PerS ||
        maximum_velocity_error > kVelocityToleranceMPerS ||
        maximum_tracer_q_error > kTracerQToleranceM ||
        maximum_tracer_concentration_error > kTracerConcentrationTolerance ||
        source_ledger_error > source_ledger_tolerance_m3 ||
        sink_ledger_error > sink_ledger_tolerance_m3 ||
        boundary_ledger_error > boundary_ledger_tolerance_m3 ||
        tracer_source_ledger_error > tracer_source_ledger_tolerance_m3 ||
        tracer_sink_ledger_error > tracer_sink_ledger_tolerance_m3 ||
        tracer_boundary_ledger_error > tracer_boundary_ledger_tolerance_m3 ||
        tracer_amount_error > tracer_amount_tolerance_m3 ||
        std::abs(tracer_conservation_residual_m3) > tracer_conservation_tolerance_m3) {
        throw std::runtime_error(
            "fluid 2.5D finite-volume GPU oracle mismatch: status=" +
            std::to_string(status_flags) + " max_depth=" + std::to_string(maximum_depth_error) +
            " max_momentum=" + std::to_string(maximum_momentum_error) +
            " max_velocity=" + std::to_string(maximum_velocity_error) +
            " velocity_cell=" + std::to_string(maximum_velocity_error_cell) +
            " velocity_component=" + std::to_string(maximum_velocity_error_component) +
            " velocity_actual=" + std::to_string(maximum_velocity_actual) +
            " velocity_expected=" + std::to_string(maximum_velocity_expected) +
            " velocity_actual_depth=" +
            std::to_string(actual_depth[maximum_velocity_error_cell]) +
            " velocity_expected_depth=" +
            std::to_string(
                finite_volume_oracle_->water_depth_m()[maximum_velocity_error_cell]) +
            " max_tracer_q=" + std::to_string(maximum_tracer_q_error) +
            " max_tracer_concentration=" + std::to_string(maximum_tracer_concentration_error) +
            " source_ledger=" + std::to_string(source_ledger_error) +
            " sink_ledger=" + std::to_string(sink_ledger_error) +
            " boundary_ledger=" + std::to_string(boundary_ledger_error) +
            " tracer_amount=" + std::to_string(tracer_amount_error) +
            " tracer_source_ledger=" + std::to_string(tracer_source_ledger_error) +
            " tracer_sink_ledger=" + std::to_string(tracer_sink_ledger_error) +
            " tracer_boundary_ledger=" + std::to_string(tracer_boundary_ledger_error) +
            " tracer_conservation=" + std::to_string(tracer_conservation_residual_m3));
    }
    std::printf(
        "fluid_25d_gpu_oracle: PASS solver=finite-volume scenario=%s status=%u "
        "max_depth=%.7f max_momentum=%.7f max_velocity=%.7f max_tracer_q=%.7f "
        "max_tracer_concentration=%.7f tracer_amount=%.7f source_ledger=%.7f sink_ledger=%.7f "
        "boundary_ledger=%.7f tracer_source_ledger=%.7f tracer_sink_ledger=%.7f "
        "tracer_boundary_ledger=%.7f tracer_conservation=%.7f\n",
        fluid_25d_scenario_name(config_.simulation.scenario), status_flags, maximum_depth_error,
        maximum_momentum_error, maximum_velocity_error, maximum_tracer_q_error,
        maximum_tracer_concentration_error, tracer_amount_error, source_ledger_error,
        sink_ledger_error, boundary_ledger_error, tracer_source_ledger_error,
        tracer_sink_ledger_error, tracer_boundary_ledger_error,
        tracer_conservation_residual_m3);
}

} // namespace cubey::projects::fluid::fluid_25d::builtin_detail
