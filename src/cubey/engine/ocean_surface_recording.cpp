#include <cubey/engine/ocean_surface_runtime.h>

#include <cubey/render/color_space.h>
#include <cubey/render/material.h>
#include <cubey/render/pass.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/memory_barriers.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "ocean_surface_internal.h"

namespace cubey {

namespace {

using render::OceanFieldPrecision;
using namespace ocean_surface_detail;

[[nodiscard]] bool ocean_should_update_cascade(const render::OceanSurfaceConfig& config,
                                               std::uint32_t cascade, std::uint64_t frame_index,
                                               bool first_update) {
    if (!render::ocean_cascade_enabled(config, cascade)) {
        return false;
    }
    if (first_update) {
        return true;
    }
    const std::uint32_t interval = render::ocean_cascade_update_interval(config, cascade);
    return interval <= 1U || (frame_index % interval) == 0U;
}

[[nodiscard]] std::uint32_t log2_exact(std::uint32_t value) {
    if (!render::ocean_is_power_of_two(value)) {
        throw std::runtime_error("ocean FFT resolution must be a power of two");
    }
    std::uint32_t result = 0U;
    while (value > 1U) {
        value >>= 1U;
        ++result;
    }
    return result;
}

[[nodiscard]] render::ComputeDispatchGroups
ocean_dispatch_groups(const render::OceanSurfaceConfig& config, std::uint32_t cascade) {
    const std::uint32_t map_size = render::ocean_cascade_map_size(config, cascade);
    return render::ceil_dispatch_groups(map_size, map_size, 16U);
}

[[nodiscard]] std::string ocean_gpu_timing_label(const char* phase, std::uint32_t cascade) {
    return std::string("ocean.") + phase + ".c" + std::to_string(cascade);
}

[[nodiscard]] float ocean_jonswap_alpha(float wind_speed, float fetch_length_m) {
    constexpr float gravity = 9.81F;
    return 0.076F *
           std::pow((wind_speed * wind_speed) / std::max(fetch_length_m * gravity, 0.001F), 0.22F);
}

[[nodiscard]] float ocean_jonswap_peak_frequency(float wind_speed, float fetch_length_m) {
    constexpr float gravity = 9.81F;
    return 22.0F * std::pow((gravity * gravity) / std::max(wind_speed * fetch_length_m, 0.001F),
                            1.0F / 3.0F);
}

[[nodiscard]] OceanSurfacePushConstants ocean_surface_push_constants(
    const render::OceanSurfaceConfig& config, const OceanSurfaceRuntimeFrameInfo& frame,
    const render::OceanSurfaceFrame& surface_frame, const render::OceanMeshPatch& patch,
    const render::OceanPatchShadingPlan& shading_plan) {
    const float debug_z = frame.debug_view == render::OceanRenderView::Exposure
                              ? frame.exposure
                              : static_cast<float>(surface_frame.mesh_config.mesh_lod_levels - 1U);
    return {
        .view_projection = frame.view_projection,
        .camera_time =
            {
                frame.camera_position_m.x,
                frame.camera_position_m.y,
                frame.camera_position_m.z,
                static_cast<float>(frame.elapsed_seconds),
            },
        .mesh_options =
            {
                static_cast<float>(patch.cells_x),
                static_cast<float>(patch.cells_z),
                surface_frame.mesh_config.mesh_extent,
                config.horizon_fog,
            },
        .patch_bounds =
            {
                patch.bounds.min_x,
                patch.bounds.max_x,
                patch.bounds.min_z,
                patch.bounds.max_z,
            },
        .debug_options =
            {
                static_cast<float>(static_cast<std::uint32_t>(frame.debug_view)),
                static_cast<float>(patch.level),
                debug_z,
                frame.wire_overlay ? std::clamp(frame.wire_opacity, 0.0F, 1.0F) : 0.0F,
            },
        .inspection_options =
            {
                static_cast<float>(frame.selected_cascade),
                config.shape_anti_repeat_strength,
                config.foam_density,
                config.foam_sharpness,
            },
        .tile_lengths =
            {
                config.cascades[0].tile_length,
                config.cascades[1].tile_length,
                config.cascades[2].tile_length,
                config.cascades[3].tile_length,
            },
        .displacement_scales =
            {
                config.cascades[0].displacement_scale,
                config.cascades[1].displacement_scale,
                config.cascades[2].displacement_scale,
                config.cascades[3].displacement_scale,
            },
        .normal_scales =
            {
                config.cascades[0].normal_scale,
                config.cascades[1].normal_scale,
                config.cascades[2].normal_scale,
                config.cascades[3].normal_scale,
            },
        .cascade4_options =
            {
                config.cascades[4].tile_length,
                config.cascades[4].displacement_scale,
                config.cascades[4].normal_scale,
                static_cast<float>(render::ocean_cascade_map_size(config, 4U)),
            },
        .water_color = {config.water_color_r, config.water_color_g, config.water_color_b,
                        config.roughness},
        .foam_color = {config.foam_color_r, config.foam_color_g, config.foam_color_b,
                       config.normal_strength},
        .quality_options =
            {
                static_cast<float>(shading_plan.self_shadow_steps),
                shading_plan.conservative_footprint_m,
                static_cast<float>(shading_plan.detail_filter),
                shading_plan.self_shadow_reduced ? 1.0F : 0.0F,
            },
    };
}

[[nodiscard]] OceanSpectrumPushConstants
ocean_spectrum_push_constants(const render::OceanSurfaceConfig& config,
                              std::uint32_t cascade_index) {
    const render::OceanCascadeConfig& cascade = render::ocean_cascade(config, cascade_index);
    const render::OceanCascadeDomain domain =
        config.spectral_domains_enabled ? render::ocean_cascade_domain(config, cascade_index)
                                        : render::OceanCascadeDomain{};
    const float fetch_m = cascade.fetch_length_km * 1000.0F;
    return {
        .seed_tile =
            {
                static_cast<float>(cascade.seed_x),
                static_cast<float>(cascade.seed_y),
                cascade.tile_length,
                cascade.tile_length,
            },
        .spectrum_options =
            {
                ocean_jonswap_alpha(cascade.wind_speed, fetch_m),
                ocean_jonswap_peak_frequency(cascade.wind_speed, fetch_m),
                cascade.wind_speed,
                cascade.wind_direction_degrees * render::kOceanPi / 180.0F,
            },
        .shape_options =
            {
                config.depth,
                cascade.swell,
                cascade.detail,
                cascade.spread,
            },
        .cascade_options =
            {
                static_cast<float>(cascade_index),
                static_cast<float>(render::ocean_cascade_map_size(config, cascade_index)),
                domain.active ? domain.low_k : 0.0F,
                domain.active ? domain.high_k : 0.0F,
            },
    };
}

[[nodiscard]] OceanModulatePushConstants
ocean_modulate_push_constants(const render::OceanSurfaceConfig& config, std::uint32_t cascade_index,
                              double elapsed_seconds) {
    const render::OceanCascadeConfig& cascade = render::ocean_cascade(config, cascade_index);
    return {
        .tile_depth_time =
            {
                cascade.tile_length,
                cascade.tile_length,
                config.depth,
                static_cast<float>(elapsed_seconds) + cascade.time_offset,
            },
        .cascade_options =
            {
                static_cast<float>(cascade_index),
                static_cast<float>(render::ocean_cascade_map_size(config, cascade_index)),
                0.0F,
                0.0F,
            },
    };
}

[[nodiscard]] OceanFftPushConstants
ocean_fft_push_constants(const render::OceanSurfaceConfig& config, std::uint32_t cascade_index,
                         std::uint32_t stage, bool horizontal, bool first_pass) {
    return {
        .fft_options =
            {
                static_cast<float>(render::ocean_cascade_map_size(config, cascade_index)),
                static_cast<float>(stage),
                horizontal ? 1.0F : 0.0F,
                first_pass ? 1.0F : 0.0F,
            },
        .pass_options = {},
    };
}

[[nodiscard]] OceanUnpackPushConstants
ocean_unpack_push_constants(const render::OceanSurfaceConfig& config, std::uint32_t cascade_index,
                            double delta_seconds, bool foam_initialized) {
    const render::OceanCascadeConfig& cascade = render::ocean_cascade(config, cascade_index);
    const float delta = static_cast<float>(delta_seconds > 0.0 ? delta_seconds : (1.0 / 60.0));
    return {
        .foam_options =
            {
                cascade.whitecap,
                delta * cascade.foam_amount * 7.5F,
                delta * std::max(0.5F, 10.0F - cascade.foam_amount) * 1.15F,
                foam_initialized ? 1.0F : 0.0F,
            },
        .cascade_options =
            {
                static_cast<float>(cascade_index),
                static_cast<float>(render::ocean_cascade_map_size(config, cascade_index)),
                0.0F,
                0.0F,
            },
    };
}


} // namespace

void OceanSurfaceRuntime::record_update(const vulkan::CommandRecorder& recorder,
                                        render::FrameSlot frame_slot) {
    if (!frame_prepared_ || frame_slot.index != prepared_frame_slot_.index ||
        frame_slot.count != prepared_frame_slot_.count) {
        throw std::runtime_error("ocean surface runtime frame was not prepared");
    }
    if (profiler_.has_value()) {
        profiler_->begin_frame(recorder.handle(), frame_slot.index);
    }
    if (!textures_initialized_) {
        recorder.transition_image_layout(
            vulkan::begin_storage_image_write_transition(fallback_field().handle()));
        for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
            if (!cascade_allocated(cascade)) {
                continue;
            }
            recorder.transition_image_layout(
                vulkan::begin_storage_image_write_transition(h0(cascade).handle()));
            for (std::uint32_t field_index = 0U; field_index < render::kOceanSpectrumFieldCount;
                 ++field_index) {
                recorder.transition_image_layout(vulkan::begin_storage_image_write_transition(
                    field(cascade, field_index).handle()));
                recorder.transition_image_layout(vulkan::begin_storage_image_write_transition(
                    ping(cascade, field_index).handle()));
                recorder.transition_image_layout(vulkan::begin_storage_image_write_transition(
                    pong(cascade, field_index).handle()));
            }
            recorder.transition_image_layout(
                vulkan::begin_storage_image_write_transition(displacement(cascade).handle()));
            recorder.transition_image_layout(
                vulkan::begin_storage_image_write_transition(normal(cascade).handle()));
            recorder.transition_image_layout(
                vulkan::begin_storage_image_write_transition(foam(cascade).handle()));
        }
        textures_initialized_ = true;
    }

    if (!spectrum_initialized_) {
        for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
            if (!ocean_should_update_cascade(ocean_config_, cascade, compute_frame_index_,
                                             !foam_initialized_)) {
                continue;
            }
            vulkan::GpuTimestampScope scope(profiler(), recorder.handle(), frame_slot.index,
                                            ocean_gpu_timing_label("spectrum", cascade));
            render::record_compute_pipeline_dispatch(
                recorder,
                render::compute_pipeline_dispatch_info(
                    spectrum_pipeline(), spectrum_set(cascade),
                    ocean_dispatch_groups(ocean_config_, cascade)),
                VK_SHADER_STAGE_COMPUTE_BIT, ocean_spectrum_push_constants(ocean_config_, cascade));
        }
        vulkan::record_compute_shader_write_barrier(recorder.handle());
        spectrum_initialized_ = true;
    }

    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        vulkan::GpuTimestampScope scope(profiler(), recorder.handle(), frame_slot.index,
                                        ocean_gpu_timing_label("modulate", cascade));
        render::record_compute_pipeline_dispatch(
            recorder,
            render::compute_pipeline_dispatch_info(modulate_pipeline(), modulate_set(cascade),
                                                   ocean_dispatch_groups(ocean_config_, cascade)),
            VK_SHADER_STAGE_COMPUTE_BIT,
            ocean_modulate_push_constants(ocean_config_, cascade, frame_.elapsed_seconds));
    }
    vulkan::record_compute_shader_write_barrier(recorder.handle());

    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        vulkan::GpuTimestampScope scope(profiler(), recorder.handle(), frame_slot.index,
                                        ocean_gpu_timing_label("fft", cascade));
        const render::ComputeDispatchGroups groups = ocean_dispatch_groups(ocean_config_, cascade);
        const std::uint32_t stage_count =
            log2_exact(render::ocean_cascade_map_size(ocean_config_, cascade));
        for (std::uint32_t field_index = 0U; field_index < render::kOceanSpectrumFieldCount;
             ++field_index) {
            bool source_is_ping = true;
            for (std::uint32_t stage = 1U; stage <= stage_count; ++stage) {
                const std::uint32_t set_index = stage == 1U ? 0U : (source_is_ping ? 1U : 2U);
                render::record_compute_pipeline_dispatch(
                    recorder,
                    render::compute_pipeline_dispatch_info(
                        fft_pipeline(), fft_set(cascade, field_index, set_index), groups),
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    ocean_fft_push_constants(ocean_config_, cascade, stage, true, stage == 1U));
                vulkan::record_compute_shader_write_barrier(recorder.handle());
                source_is_ping = stage == 1U ? true : !source_is_ping;
            }
            for (std::uint32_t stage = 1U; stage <= stage_count; ++stage) {
                const std::uint32_t set_index = source_is_ping ? 1U : 2U;
                render::record_compute_pipeline_dispatch(
                    recorder,
                    render::compute_pipeline_dispatch_info(
                        fft_pipeline(), fft_set(cascade, field_index, set_index), groups),
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    ocean_fft_push_constants(ocean_config_, cascade, stage, false, stage == 1U));
                vulkan::record_compute_shader_write_barrier(recorder.handle());
                source_is_ping = !source_is_ping;
            }
        }
    }

    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        vulkan::GpuTimestampScope scope(profiler(), recorder.handle(), frame_slot.index,
                                        ocean_gpu_timing_label("unpack", cascade));
        render::record_compute_pipeline_dispatch(
            recorder,
            render::compute_pipeline_dispatch_info(unpack_pipeline(), unpack_set(cascade),
                                                   ocean_dispatch_groups(ocean_config_, cascade)),
            VK_SHADER_STAGE_COMPUTE_BIT,
            ocean_unpack_push_constants(ocean_config_, cascade, frame_.delta_seconds,
                                        foam_initialized_));
    }
    vulkan::record_shader_write_barrier(recorder.handle(),
                                        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_SHADER_READ_BIT);
    foam_initialized_ = true;
    ++compute_frame_index_;
}

void OceanSurfaceRuntime::record_surface_draws(const vulkan::CommandRecorder& recorder,
                                               render::FrameSlot frame_slot) const {
    if (!frame_prepared_ || frame_slot.index != prepared_frame_slot_.index ||
        frame_slot.count != prepared_frame_slot_.count) {
        throw std::runtime_error("ocean surface runtime frame was not prepared");
    }
    const render::GraphicsPipelineResource* bound_pipeline = nullptr;
    for (std::size_t index = 0U; index < patches_.count; ++index) {
        if (!visible_patches_[index]) {
            continue;
        }
        const render::OceanMeshPatch& patch = patches_.patches[index];
        const render::OceanPatchShadingPlan& shading_plan = shading_plans_[index];
        const render::GraphicsPipelineResource& pipeline =
            surface_pipeline(shading_plan.detail_filter);
        if (bound_pipeline != &pipeline) {
            recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0U,
                                         surface_set(frame_slot));
            bound_pipeline = &pipeline;
        }
        recorder.push_constants(
            pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U,
            ocean_surface_push_constants(ocean_config_, frame_, draw_plan_.surface_frame, patch,
                                         shading_plan));
        recorder.draw(render::ocean_mesh_patch_vertex_count(patch));
    }
}


} // namespace cubey
