#include "terrain_app_internal.h"

namespace cubey::projects::terrain::detail {

void TerrainApp::record_metrics(cubey::profiling::ProfileRecorder* recorder,
                                std::uint64_t frame_index) const {
    if (recorder == nullptr) {
        return;
    }
    recorder->record_metric(frame_index, "terrain.backdrop", "submitted_sectors",
                            terrain_runtime_.draw_plan().submitted_sector_count);
    recorder->record_metric(frame_index, "terrain.backdrop", "submitted_triangles",
                            terrain_runtime_.draw_plan().submitted_triangle_count);
    const cubey::StagedResourceStatus& build_status = product_builds_.status();
    recorder->record_metric(frame_index, "terrain.initialization", "generation",
                            static_cast<double>(build_status.generation.id));
    recorder->record_metric(frame_index, "terrain.initialization", "phase",
                            static_cast<double>(static_cast<std::uint8_t>(build_status.phase)));
    recorder->record_metric(frame_index, "terrain.initialization", "prepare_ms",
                            build_status.prepare_milliseconds);
    recorder->record_metric(frame_index, "terrain.initialization", "install_ms",
                            build_status.install_milliseconds);
    recorder->record_metric(frame_index, "terrain.initialization", "resident",
                            terrain_runtime_.product_ready() ? 1.0 : 0.0);
    recorder->record_metric(frame_index, "terrain.initialization", "source_load_ms",
                            source_load_milliseconds_);
    recorder->record_metric(frame_index, "terrain.initialization", "climate_load_ms",
                            climate_load_milliseconds_);
    recorder->record_metric(frame_index, "terrain.initialization", "placement_ms",
                            placement_milliseconds_);
    recorder->record_metric(
        frame_index, "terrain.initialization", "product_cache_hit",
        product_cache_diagnostics_.source == TerrainProductPreparationSource::Cache ? 1.0
                                                                                    : 0.0);
    recorder->record_metric(
        frame_index, "terrain.initialization", "product_cache_lookup",
        static_cast<double>(static_cast<std::uint8_t>(product_cache_diagnostics_.lookup)));
    recorder->record_metric(frame_index, "terrain.initialization", "product_cache_stored",
                            product_cache_diagnostics_.stored ? 1.0 : 0.0);
    recorder->record_metric(frame_index, "terrain.initialization", "product_cache_load_ms",
                            product_cache_diagnostics_.load_milliseconds);
    recorder->record_metric(frame_index, "terrain.initialization", "product_cache_decode_ms",
                            product_cache_diagnostics_.decode_milliseconds);
    recorder->record_metric(frame_index, "terrain.initialization",
                            "product_cache_generation_ms",
                            product_cache_diagnostics_.generation_milliseconds);
    recorder->record_metric(frame_index, "terrain.initialization", "product_cache_encode_ms",
                            product_cache_diagnostics_.encode_milliseconds);
    recorder->record_metric(frame_index, "terrain.initialization", "product_cache_store_ms",
                            product_cache_diagnostics_.store_milliseconds);
    recorder->record_metric(
        frame_index, "terrain.backdrop", "product_render_triangles",
        static_cast<double>(product_info_.diagnostics.render_triangle_count));
    recorder->record_metric(
        frame_index, "terrain.backdrop", "center_render_triangles",
        static_cast<double>(product_info_.diagnostics.center_render_triangle_count));
    recorder->record_metric(frame_index, "terrain.backdrop", "source_samples",
                            static_cast<double>(product_info_.diagnostics.source_sample_count));
    recorder->record_metric(
        frame_index, "terrain.backdrop", "sampled_vertices",
        static_cast<double>(product_info_.diagnostics.sampled_vertex_count));
    recorder->record_metric(frame_index, "terrain.backdrop", "render_vertices",
                            static_cast<double>(product_info_.diagnostics.render_vertex_count));
    const double retained_vertex_ratio =
        product_info_.diagnostics.sampled_vertex_count == 0U
            ? 0.0
            : static_cast<double>(product_info_.diagnostics.render_vertex_count) /
                  static_cast<double>(product_info_.diagnostics.sampled_vertex_count);
    recorder->record_metric(frame_index, "terrain.backdrop", "vertex_compaction_ratio",
                            retained_vertex_ratio);
    recorder->record_metric(frame_index, "terrain.backdrop", "mesh_upload_bytes",
                            static_cast<double>(terrain_runtime_.mesh_upload_bytes()));
    recorder->record_metric(frame_index, "terrain.backdrop", "mesh_upload_transfer_submissions",
                            terrain_runtime_.mesh_upload_transfer_submissions());
    recorder->record_metric(frame_index, "terrain.backdrop", "content_hash_low32",
                            static_cast<double>(static_cast<std::uint32_t>(
                                product_info_.diagnostics.content_hash)));
    recorder->record_metric(frame_index, "terrain.backdrop", "content_hash_high32",
                            static_cast<double>(static_cast<std::uint32_t>(
                                product_info_.diagnostics.content_hash >> 32U)));
    recorder->record_metric(frame_index, "terrain.backdrop", "geometry_hash_low32",
                            static_cast<double>(static_cast<std::uint32_t>(
                                product_info_.diagnostics.geometry_hash)));
    recorder->record_metric(frame_index, "terrain.backdrop", "geometry_hash_high32",
                            static_cast<double>(static_cast<std::uint32_t>(
                                product_info_.diagnostics.geometry_hash >> 32U)));
    recorder->record_metric(frame_index, "terrain.surface", "model",
                            static_cast<double>(runtime_config_.surface_model));
    recorder->record_metric(frame_index, "terrain.surface", "mean_rock",
                            product_info_.diagnostics.mean_rock);
    recorder->record_metric(frame_index, "terrain.surface", "mean_snow",
                            product_info_.diagnostics.mean_snow);
    recorder->record_metric(frame_index, "terrain.surface", "mean_vegetation",
                            product_info_.diagnostics.mean_vegetation);
    recorder->record_metric(frame_index, "terrain.surface", "mean_moisture",
                            product_info_.diagnostics.mean_moisture);
    recorder->record_metric(frame_index, "terrain.surface", "climate_bound",
                            climate_source_.has_value() ? 1.0 : 0.0);
    if (climate_source_.has_value()) {
        const std::string_view hash = climate_source_->metadata().climate_sha256;
        recorder->record_metric(frame_index, "terrain.surface", "climate_hash_first32",
                                static_cast<double>(sha256_word(hash, 0U)));
        recorder->record_metric(frame_index, "terrain.surface", "climate_hash_last32",
                                static_cast<double>(sha256_word(hash, hash.size() - 8U)));
    }
    const TerrainBackdropClimateDiagnostics& climate = climate_diagnostics_;
    recorder->record_metric(frame_index, "terrain.climate", "sample_count",
                            static_cast<double>(climate.sample_count));
    recorder->record_metric(frame_index, "terrain.climate", "mean_temperature_c",
                            climate.mean_temperature_c);
    recorder->record_metric(frame_index, "terrain.climate", "mean_temperature_stddev_c",
                            climate.mean_temperature_stddev_c);
    recorder->record_metric(frame_index, "terrain.climate", "mean_precipitation_annual_mm",
                            climate.mean_precipitation_annual_mm);
    recorder->record_metric(frame_index, "terrain.climate", "mean_precipitation_cv",
                            climate.mean_precipitation_cv);
    recorder->record_metric(frame_index, "terrain.climate", "mean_growing_season_days",
                            climate.mean_growing_season_days);
    recorder->record_metric(frame_index, "terrain.climate", "mean_thermal_growth",
                            climate.mean_thermal_growth);
    recorder->record_metric(frame_index, "terrain.climate",
                            "mean_thermal_water_demand_proxy_mm",
                            climate.mean_thermal_water_demand_proxy_mm);
    recorder->record_metric(frame_index, "terrain.climate", "mean_climate_moisture_ratio",
                            climate.mean_climate_moisture_ratio);
    recorder->record_metric(frame_index, "terrain.climate", "mean_seasonality_factor",
                            climate.mean_seasonality_factor);
    recorder->record_metric(frame_index, "terrain.climate", "mean_effective_moisture",
                            climate.mean_effective_moisture);
    recorder->record_metric(frame_index, "terrain.climate", "mean_moisture_weight",
                            climate.mean_moisture_weight);
    recorder->record_metric(frame_index, "terrain.climate", "mean_cover_weight",
                            climate.mean_cover_weight);
    recorder->record_metric(frame_index, "terrain.climate", "mean_annual_cold_potential",
                            climate.mean_annual_cold_potential);
    recorder->record_metric(frame_index, "terrain.climate", "mean_wet_snow_potential",
                            climate.mean_wet_snow_potential);
    recorder->record_metric(frame_index, "terrain.backdrop", "render_stride",
                            static_cast<double>(product_info_.request.render_stride));
    recorder->record_metric(frame_index, "terrain.backdrop", "outer_radius_m",
                            product_info_.request.outer_radius_m);
    recorder->record_metric(frame_index, "terrain.placement", "mode",
                            static_cast<double>(placement_stage_.mode));
    recorder->record_metric(frame_index, "terrain.placement", "sample_index",
                            static_cast<double>(placement_stage_.sample_index));
    recorder->record_metric(frame_index, "terrain.placement", "source_focus_x_m",
                            placement_stage_.placement.source_focus_xz.x);
    recorder->record_metric(frame_index, "terrain.placement", "source_focus_z_m",
                            placement_stage_.placement.source_focus_xz.y);
    recorder->record_metric(frame_index, "terrain.placement", "local_contract",
                            placement_stage_.placement.local_contract_satisfied ? 1.0 : 0.0);
    recorder->record_metric(frame_index, "terrain.placement", "directional_contract",
                            placement_stage_.placement.contract_satisfied ? 1.0 : 0.0);
    recorder->record_metric(frame_index, "terrain.placement", "score",
                            placement_stage_.placement.score);
    recorder->record_metric(frame_index, "terrain.placement", "local_radius_m",
                            placement_stage_.placement.local_radius_m);
    recorder->record_metric(frame_index, "terrain.placement", "local_relief_m",
                            placement_stage_.placement.local_relief_m);
    recorder->record_metric(frame_index, "terrain.placement", "maximum_local_relief_m",
                            placement_stage_.placement.maximum_local_relief_m);
    recorder->record_metric(frame_index, "terrain.placement", "local_p95_slope",
                            placement_stage_.placement.local_p95_slope);
    recorder->record_metric(frame_index, "terrain.placement", "maximum_local_p95_slope",
                            placement_stage_.placement.maximum_local_p95_slope);
    recorder->record_metric(frame_index, "terrain.placement", "baked_clearance_m",
                            placement_stage_.stage.minimum_camera_clearance_m);
    recorder->record_metric(frame_index, "terrain.placement", "foreground_height_m",
                            foreground_height_m_);
    recorder->record_metric(frame_index, "terrain.backdrop", "filtered_detail",
                            runtime_config_.material != TerrainMaterialMode::Flat ? 1.0 : 0.0);
    recorder->record_metric(frame_index, "terrain.backdrop", "material_texture_bytes",
                            static_cast<double>(terrain_runtime_.material_texture_bytes()));
    recorder->record_metric(frame_index, "terrain.shadow", "enabled",
                            runtime_config_.shadows ? 1.0 : 0.0);
    recorder->record_metric(frame_index, "terrain.shadow", "map_extent",
                            static_cast<double>(cubey::render::kTerrainShadowMapExtent));
    recorder->record_metric(frame_index, "terrain.shadow", "texel_world_m",
                            terrain_runtime_.shadow_projection().texel_world_size_m);
    recorder->record_metric(frame_index, "terrain.shadow", "depth_span_m",
                            terrain_runtime_.shadow_projection().depth_span_m);
    recorder->record_metric(frame_index, "terrain.shadow", "triangle_count",
                            static_cast<double>(shadow_caster_triangle_count()));
    recorder->record_metric(frame_index, "terrain.shadow", "cache_valid",
                            terrain_runtime_.shadow_cache().valid ? 1.0 : 0.0);
    recorder->record_metric(frame_index, "terrain.shadow", "update_count",
                            static_cast<double>(terrain_runtime_.shadow_cache().update_count));
    recorder->record_metric(frame_index, "terrain.shadow", "updated",
                            terrain_runtime_.shadow_update_this_frame() ? 1.0 : 0.0);
}

[[nodiscard]] cubey::Transform3D TerrainApp::current_camera_transform() const {
    const TerrainBackdropStagePlan& stage = placement_stage_.stage;
    const float initial_yaw =
        runtime_config_.initial_azimuth_radians.value_or(stage.showcase_yaw_radians);
    return cubey::orbit_camera_transform({
        .target = {0.0F, foreground_vertical_offset_m(), 0.0F},
        .distance = orbit_controller_.distance(),
        .yaw = orbit_controller_.yaw() + initial_yaw,
        .pitch = orbit_controller_.pitch() - stage.orbit_default_elevation_radians,
    });
}

[[nodiscard]] float TerrainApp::foreground_vertical_offset_m() const noexcept {
    return foreground_height_m_ - baked_foreground_height_m_;
}

[[nodiscard]] float TerrainApp::aspect(VkExtent2D extent) {
    return extent.height == 0U
               ? 1.0F
               : static_cast<float>(extent.width) / static_cast<float>(extent.height);
}

[[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
TerrainApp::atmosphere_uniforms(VkExtent2D extent) const {
    const float physical_camera_height_m =
        frame_camera_transform_.translation.y + placement_stage_.stage.target_height_m;
    return cubey::render::atmosphere_environment_frame_uniforms(
        atmosphere_state_.environment,
        {
            .view_rays = cubey::render::view_ray_basis_3d(
                frame_camera_transform_.rotation, aspect(extent), camera_.fovy_radians()),
            .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
            .camera_position_km = {0.0F,
                                   atmosphere_state_.environment.bottom_radius_km +
                                       std::max(physical_camera_height_m, 0.0F) * 0.001F,
                                   0.0F},
            .camera_position_km_explicit = true,
        });
}

[[nodiscard]] bool TerrainApp::cloud_layer_enabled() const noexcept {
    return runtime_config_.debug_view == TerrainDebugView::Surface && clouds_config_.enabled;
}

[[nodiscard]] cubey::CloudEnvironmentRuntimeFrame TerrainApp::current_cloud_frame(
    VkExtent2D extent, const cubey::render::AtmosphereEnvironmentLighting& lighting) const {
    const cubey::render::ViewRayBasis3D view_rays = cubey::render::view_ray_basis_3d(
        frame_camera_transform_.rotation, aspect(extent), camera_.fovy_radians());
    const float physical_camera_height_m =
        frame_camera_transform_.translation.y + placement_stage_.stage.target_height_m;
    return cloud_runtime_.frame(
        cubey::CloudEnvironmentSurfaceViewInfo{
            .camera_position = {frame_camera_transform_.translation.x,
                                std::max(physical_camera_height_m, 0.0F),
                                frame_camera_transform_.translation.z},
            .camera_right = cubey::math::Vec3{view_rays.right_aspect},
            .camera_up = cubey::math::Vec3{view_rays.up_tan_half_fovy},
            .camera_forward = cubey::math::Vec3{view_rays.forward},
            .tan_half_fovy = view_rays.up_tan_half_fovy.w,
            .target_extent = extent,
            .near_plane_m = camera_.near_z(),
            .far_plane_m = camera_.far_z(),
            .external_background = true,
            .scene_depth_mode = cubey::render::CloudLayerSceneDepthMode::OpaqueForeground,
            .scene_depth_fade_m = kTerrainCloudSceneDepthFadeM,
        },
        lighting);
}

[[nodiscard]] float TerrainApp::display_exposure() const {
    return atmosphere_state_.resolved_exposure;
}

void TerrainApp::record_shadow_pass(const cubey::vulkan::CommandRecorder& recorder) const {
    terrain_runtime_.record_shadow_pass(recorder);
}

void TerrainApp::record_terrain_pass(const cubey::vulkan::CommandRecorder& recorder,
                                     cubey::render::ColorTargetView color,
                                     cubey::render::DepthTargetView depth,
                                     cubey::render::FrameSlot frame_slot) const {
    const cubey::render::RenderTargetView target =
        cubey::render::render_target_view(color, depth);
    const cubey::render::RenderTargetRenderingInfo rendering(
        target,
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        cubey::render::RenderTargetAttachmentOps{
            .color = cubey::vulkan::load_store_attachment_ops(),
            .depth = cubey::vulkan::clear_discard_attachment_ops(),
        });
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(color.extent);
    terrain_runtime_.record_surface_draws(recorder, frame_slot);
    recorder.end_rendering();
}

void TerrainApp::record_stage_proxy_pass(const cubey::vulkan::CommandRecorder& recorder,
                                         cubey::render::ColorTargetView color,
                                         cubey::render::DepthTargetView depth,
                                         cubey::render::FrameSlot frame_slot,
                                         bool clear_depth) const {
    const cubey::render::RenderTargetView target =
        cubey::render::render_target_view(color, depth);
    const cubey::render::RenderTargetRenderingInfo rendering(
        target,
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        cubey::render::RenderTargetAttachmentOps{
            .color = cubey::vulkan::load_store_attachment_ops(),
            .depth = clear_depth ? cubey::vulkan::clear_discard_attachment_ops()
                                 : cubey::vulkan::load_store_attachment_ops(),
        });
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(color.extent);
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, stage_proxy_pipeline().pipeline());
    terrain_runtime_.bind_environment(recorder, stage_proxy_pipeline(), frame_slot);
    recorder.push_constants(
        stage_proxy_pipeline().layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        TerrainStageProxyPushConstants{
            .view_projection =
                camera_.view_projection_matrix(frame_camera_transform_, aspect(color.extent)),
            .camera_position = {frame_camera_transform_.translation.x,
                                frame_camera_transform_.translation.y,
                                frame_camera_transform_.translation.z, 0.0F},
            .object_translation = {0.0F, foreground_vertical_offset_m(), 0.0F, 0.0F},
        });
    cubey::render::record_draw_item(recorder.handle(),
                                    cubey::render::DrawItem{.mesh = &stage_proxy_mesh()});
    recorder.end_rendering();
}

void TerrainApp::record_placeholder_depth_pass(const cubey::vulkan::CommandRecorder& recorder,
                                               cubey::render::ColorTargetView color,
                                               cubey::render::DepthTargetView depth) {
    const cubey::render::RenderTargetRenderingInfo rendering(
        cubey::render::render_target_view(color, depth),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        cubey::render::RenderTargetAttachmentOps{
            .color = cubey::vulkan::load_store_attachment_ops(),
            .depth = cubey::vulkan::clear_discard_attachment_ops(),
        });
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(color.extent);
    recorder.end_rendering();
}

[[nodiscard]] CompiledTerrainGraph TerrainApp::current_render_graph(
    cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
    cubey::render::RenderGraphTextureState target_initial_state,
    cubey::render::RenderGraphTextureState target_final_state,
    const std::optional<cubey::CloudEnvironmentRuntimeFrame>& cloud_runtime_frame) const {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
        "terrain backbuffer", target, target_initial_state, target_final_state);
    const cubey::render::RenderGraphTextureHandle scene_color =
        graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
            "terrain scene color", target.extent, kTerrainSceneColorFormat));
    cubey::render::RenderGraphTextureHandle post_scene_color = scene_color;
    cubey::render::CloudLayerRuntimeFrame cloud_frame{};
    const cubey::render::RenderGraphTextureHandle depth = graph.import_depth_target(
        "terrain depth", cubey::render::depth_target_view(*scene_depth_),
        cubey::render::render_graph_undefined_texture_state());
    const bool terrain_ready = terrain_runtime_.target_resources_created();
    std::optional<cubey::render::RenderGraphTextureHandle> shadow_depth;
    if (terrain_ready) {
        const std::optional<cubey::render::RenderGraphTextureState> shadow_initial_state =
            terrain_runtime_.shadow_depth_is_sampled()
                ? cubey::render::render_graph_sampled_depth_texture_state()
                : cubey::render::render_graph_undefined_texture_state();
        shadow_depth = graph.import_depth_target("terrain shadow depth",
                                                 terrain_runtime_.shadow_depth_target(),
                                                 shadow_initial_state);

        if (terrain_runtime_.shadow_update_this_frame()) {
            graph.add_pass("terrain shadow", cubey::render::RenderGraphQueueDomain::Graphics)
                .write_depth(shadow_depth.value())
                .material_pass(terrain_runtime_.shadow_material_pass())
                .execute([this](const cubey::render::RenderGraphExecutionContext& context) {
                    record_shadow_pass(context.recorder());
                });
        }
    }

    graph.add_pass("terrain atmosphere", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(scene_color)
        .material_pass(cubey::render::atmosphere_background_pass_info())
        .execute([this, scene_color,
                  frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
            atmosphere_background_.record_pass(
                context.recorder(),
                cubey::render::resolved_color_target_view(context, scene_color), frame_slot);
        });
    if (terrain_ready) {
        graph.add_pass("terrain surface", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(shadow_depth.value())
            .write_color(scene_color)
            .write_depth(depth)
            .material_pass(terrain_runtime_.surface_material_pass())
            .execute([this, scene_color, depth,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_terrain_pass(
                    context.recorder(),
                    cubey::render::resolved_color_target_view(context, scene_color),
                    cubey::render::resolved_depth_target_view(context, depth), frame_slot);
            });
    }
    if (runtime_config_.foreground_sphere) {
        graph.add_pass("terrain stage proxy", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(depth)
            .material_pass(terrain_stage_proxy_pass_info())
            .execute([this, scene_color, depth, frame_slot, terrain_ready](
                         const cubey::render::RenderGraphExecutionContext& context) {
                record_stage_proxy_pass(
                    context.recorder(),
                    cubey::render::resolved_color_target_view(context, scene_color),
                    cubey::render::resolved_depth_target_view(context, depth), frame_slot,
                    !terrain_ready);
            });
    } else if (!terrain_ready) {
        graph
            .add_pass("terrain placeholder depth",
                      cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(depth)
            .execute([scene_color,
                      depth](const cubey::render::RenderGraphExecutionContext& context) {
                record_placeholder_depth_pass(
                    context.recorder(),
                    cubey::render::resolved_color_target_view(context, scene_color),
                    cubey::render::resolved_depth_target_view(context, depth));
            });
    }
    const bool clouds_enabled = cloud_runtime_frame.has_value() && cloud_runtime_frame->enabled;
    if (clouds_enabled) {
        const cubey::render::RenderGraphTextureHandle cloud_scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "terrain cloud scene color", target.extent, kTerrainSceneColorFormat));
        cloud_frame = cloud_runtime_.declare_surface_product(graph, frame_slot,
                                                             cloud_runtime_frame.value());
        cloud_runtime_.declare_surface_composite(graph, cloud_scene_color, cloud_frame,
                                                 frame_slot, scene_color, depth);
        post_scene_color = cloud_scene_color;
    }
    graph.add_pass("terrain post", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(post_scene_color)
        .write_color(backbuffer)
        .material_pass(cubey::render::pbr_post_pass_info())
        .execute([this, target,
                  frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
            hdr_post_frame_.record_pass(context.recorder(), target, frame_slot);
        });
    return {
        .graph = graph.compile(),
        .scene_color = scene_color,
        .post_scene_color = post_scene_color,
        .scene_depth = depth,
        .cloud = cloud_frame,
        .clouds_enabled = clouds_enabled,
    };
}

void TerrainApp::complete_shadow_frame_recording() {
    if (terrain_runtime_.target_resources_created()) {
        terrain_runtime_.complete_frame();
    }
}

void TerrainApp::record_target(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                               cubey::render::ColorTargetView target,
                               cubey::render::FrameSlot frame_slot,
                               cubey::render::RenderGraphTextureState target_initial_state,
                               cubey::render::RenderGraphTextureState target_final_state,
                               cubey::render::RenderGraphCommandBufferMode command_buffer_mode) {
    if (capture_runtime_)
        capture_profile_slots_.at(frame_slot.index) = capture_active_;
    if (capture_active_) {
        const bool owns_commands =
            command_buffer_mode == cubey::render::RenderGraphCommandBufferMode::BeginAndEnd;
        if (owns_commands) {
            const VkCommandBufferBeginInfo begin{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .pInheritanceInfo = nullptr};
            cubey::vulkan::check(vkBeginCommandBuffer(command_buffer, &begin),
                                 "begin captured patch");
        }
        const auto transform = cubey::orbit_camera_transform(
            {.target = capture_target_,
             .distance = capture_orbit_.distance(),
             .yaw = capture_yaw_ + capture_orbit_.yaw() +
                    float(capture_review_s_) * config_.terrain_review_orbit_rad_per_s,
             .pitch = capture_pitch_ + capture_orbit_.pitch()});
        capture_runtime_->record(
            device, command_buffer, frame_slot, target,
            target_final_state.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            {capture_camera_.view_projection_matrix(transform, aspect(target.extent)),
             transform.translation},
            capture_animation_s_, config_.captured_water_visible,
            !config_.common.profile_output_prefix.empty(), config_.captured_water_context,
            config_.captured_water_ground_view == "normal"       ? 4U
            : config_.captured_water_ground_view == "clay"       ? 9U
            : config_.captured_water_ground_view == "clay-clean" ? 16U
            : config_.captured_water_ground_view == "albedo"     ? 2U
            : config_.captured_water_ground_view == "ownership"  ? 15U
                                                                 : 0U,
            config_.captured_water_detail == "fixed");
        if (owns_commands)
            cubey::vulkan::check(vkEndCommandBuffer(command_buffer), "end captured patch");
        return;
    }
    frame_camera_transform_ = current_camera_transform();
    const cubey::render::AtmosphereEnvironmentFrameUniforms atmosphere_frame =
        atmosphere_uniforms(target.extent);
    const cubey::render::AtmosphereEnvironmentLighting lighting =
        cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment);
    if (terrain_runtime_.target_resources_created()) {
        terrain_runtime_.prepare_frame(
            frame_slot,
            {
                .view_projection = camera_.view_projection_matrix(frame_camera_transform_,
                                                                  aspect(target.extent)),
                .camera_position = frame_camera_transform_.translation,
                .atmosphere = atmosphere_frame,
                .lighting = lighting,
                .debug_view = runtime_config_.debug_view,
                .material = runtime_config_.material,
                .aerial_perspective_strength = runtime_config_.aerial_perspective_strength,
                .shadows_enabled = runtime_config_.shadows,
            });
    } else {
        terrain_runtime_.prepare_environment(frame_slot, atmosphere_frame, lighting);
    }
    atmosphere_background_.upload(frame_slot, atmosphere_frame);
    std::optional<cubey::CloudEnvironmentRuntimeFrame> cloud_frame;
    if (cloud_layer_enabled()) {
        cloud_frame = current_cloud_frame(target.extent, lighting);
    }
    hdr_post_frame_.upload(frame_slot,
                           cubey::render::hdr_post_uniforms(target.format, display_exposure()));

    const CompiledTerrainGraph render_graph = current_render_graph(
        target, frame_slot, target_initial_state, target_final_state, cloud_frame);
    const auto prepare_resources = [this, &device, frame_slot, &render_graph](
                                       const cubey::render::RenderGraphResourceSet& resources) {
        hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, render_graph.graph,
                                                      resources, render_graph.post_scene_color);
        if (render_graph.clouds_enabled) {
            cloud_runtime_.update_surface_descriptors(
                device, frame_slot, render_graph.graph, resources, render_graph.cloud,
                render_graph.scene_color, render_graph.scene_depth);
        }
    };
    cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler();
    if (profiler != nullptr &&
        command_buffer_mode == cubey::render::RenderGraphCommandBufferMode::BeginAndEnd) {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        profiler->begin_frame(command_buffer, frame_slot.index);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer terrain",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                .profiler = profiler,
            },
            render_graph.graph, prepare_resources);
        complete_shadow_frame_recording();
        if (render_graph.clouds_enabled) {
            cloud_runtime_.complete_surface_frame(frame_slot, render_graph.cloud);
        }
        recorder.end("vkEndCommandBuffer terrain");
        return;
    }
    if (profiler != nullptr) {
        profiler->begin_frame(command_buffer, frame_slot.index);
    }
    graph_executor_.record(
        cubey::render::RenderGraphFrameRecordInfo{
            .device = &device,
            .command_buffer = command_buffer,
            .frame_slot = frame_slot,
            .label = "vkEndCommandBuffer terrain",
            .command_buffer_mode = command_buffer_mode,
            .profiler = profiler,
        },
        render_graph.graph, prepare_resources);
    complete_shadow_frame_recording();
    if (render_graph.clouds_enabled) {
        cloud_runtime_.complete_surface_frame(frame_slot, render_graph.cloud);
    }
}

[[nodiscard]] const cubey::render::Mesh& TerrainApp::stage_proxy_mesh() const {
    if (!stage_proxy_mesh_.has_value()) {
        throw std::runtime_error("terrain stage proxy mesh is not initialized");
    }
    return stage_proxy_mesh_.value();
}

[[nodiscard]] const cubey::render::GraphicsPipelineResource&
TerrainApp::stage_proxy_pipeline() const {
    if (!stage_proxy_pipeline_.has_value()) {
        throw std::runtime_error("terrain stage proxy pipeline is not initialized");
    }
    return stage_proxy_pipeline_.value();
}
} // namespace cubey::projects::terrain::detail
