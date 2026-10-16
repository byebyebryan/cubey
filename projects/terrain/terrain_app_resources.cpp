#include "terrain_app_internal.h"

namespace cubey::projects::terrain::detail {

void TerrainApp::start_product_build() {
    if (product_builds_.busy()) {
        return;
    }

    TerrainRuntimeConfig config = runtime_config_;
    const std::size_t requested_source_choice_index = edit_source_choice_index_;
    if (requested_source_choice_index != source_choice_index_) {
        const TerrainSourceChoice& choice = source_choices_.at(requested_source_choice_index);
        config.heightfield_path = choice.paths.heightfield;
        config.surface_fields_path = choice.paths.surface_fields;
        config.expected_seed = choice.expected_seed;
    }
    config.placement = edit_placement_mode_;
    config.placement_index = edit_placement_index_;
    config.surface_model = edit_surface_model_;
    product_rebuild_error_.clear();
    try {
        request_product_build(std::move(config), requested_source_choice_index);
    } catch (const std::exception& error) {
        product_rebuild_error_ = error.what();
    } catch (...) {
        product_rebuild_error_ = "unable to start terrain product rebuild";
    }
}

void TerrainApp::request_product_build(TerrainRuntimeConfig config,
                                       std::size_t requested_source_choice_index) {
    backdrop_requested_ = true;
    const std::string label = source_choices_.at(requested_source_choice_index).label;
    product_load_started_at_ = Clock::now();
    product_load_phase_.store(TerrainProductLoadPhase::Waiting, std::memory_order_relaxed);
    try {
        static_cast<void>(product_builds_.request(
            label,
            [this, config = std::move(config), requested_source_choice_index]() mutable {
                TerrainProductBuild build;
                build.config = config;
                build.source_choice_index = requested_source_choice_index;
                product_load_phase_.store(TerrainProductLoadPhase::LoadingHeightfield,
                                          std::memory_order_relaxed);
                const Clock::time_point source_load_started = Clock::now();
                build.replacement_source.emplace(require_heightfield(config.heightfield_path));
                build.source_load_milliseconds = elapsed_milliseconds(source_load_started);
                product_load_phase_.store(TerrainProductLoadPhase::LoadingClimate,
                                          std::memory_order_relaxed);
                const Clock::time_point climate_load_started = Clock::now();
                build.replacement_climate_source =
                    load_climate_source(config, build.replacement_source.value());
                build.climate_load_milliseconds = elapsed_milliseconds(climate_load_started);
                product_load_phase_.store(TerrainProductLoadPhase::PlanningPlacement,
                                          std::memory_order_relaxed);
                const Clock::time_point placement_started = Clock::now();
                build.placement =
                    make_placement_stage(build.replacement_source.value(), config);
                build.placement_milliseconds = elapsed_milliseconds(placement_started);
                const TerrainRasterClimateSource* climate =
                    build.replacement_climate_source ? &build.replacement_climate_source.value()
                                                     : nullptr;
                product_load_phase_.store(TerrainProductLoadPhase::PreparingProduct,
                                          std::memory_order_relaxed);
                PreparedProjectTerrainBackdropProduct prepared =
                    prepare_project_terrain_backdrop_product(
                        product_cache_,
                        cubey::terrain::terrain_backdrop_v1_product_request(
                            build.placement.stage, config.render_stride),
                        build.replacement_source.value(), config.surface_model, climate,
                        terrain_product_placement_parameter_hash(config, build.placement));
                build.product = std::move(prepared.product);
                build.climate_diagnostics = prepared.climate;
                build.cache_diagnostics = std::move(prepared.cache);
                return build;
            },
            [this](cubey::vulkan::GpuOwnerContext& owner, TerrainProductBuild&& prepared) {
                product_load_phase_.store(TerrainProductLoadPhase::UploadingProduct,
                                          std::memory_order_relaxed);
                TerrainBackdropProductInfo info =
                    cubey::terrain::terrain_backdrop_product_info(prepared.product);
                cubey::TerrainBackdropResidentProduct resident =
                    terrain_runtime_.build_resident_product(owner, prepared.product);
                return TerrainResidentBuild{
                    .prepared = std::move(prepared),
                    .product_info = std::move(info),
                    .resident = std::move(resident),
                };
            }));
    } catch (...) {
        product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
        product_load_started_at_.reset();
        throw;
    }
}

void TerrainApp::install_product_build(const cubey::vulkan::Device& device,
                                       cubey::vulkan::GpuRuntime& gpu,
                                       cubey::vulkan::GpuSubmissionTicket retire_after,
                                       TerrainResidentBuild installed) {
    if (!global_resources_created_) {
        throw std::runtime_error("terrain resources are not ready for a product rebuild");
    }

    TerrainProductBuild& build = installed.prepared;
    const bool placement_changed =
        !source_.has_value() || build.source_choice_index != source_choice_index_ ||
        build.config.placement != placement_stage_.mode ||
        build.config.placement_index != placement_stage_.sample_index;
    terrain_runtime_.install_resident_product(gpu, std::move(installed.resident), retire_after);
    source_ = std::move(build.replacement_source);
    climate_source_ = std::move(build.replacement_climate_source);
    source_choice_index_ = build.source_choice_index;
    placement_stage_ = std::move(build.placement);
    climate_diagnostics_ = build.climate_diagnostics;
    product_cache_diagnostics_ = std::move(build.cache_diagnostics);
    source_load_milliseconds_ = build.source_load_milliseconds;
    climate_load_milliseconds_ = build.climate_load_milliseconds;
    placement_milliseconds_ = build.placement_milliseconds;
    product_info_ = std::move(installed.product_info);
    runtime_config_ = std::move(build.config);
    camera_.set_projection(camera_.fovy_radians(), camera_.near_z(),
                           product_info_.request.outer_radius_m * 5.0F);
    if (placement_changed) {
        configure_camera_for_placement(false);
    }
    if (terrain_target_info_.has_value() && !terrain_runtime_.target_resources_created()) {
        terrain_runtime_.create_target_resources(device, terrain_target_info_.value());
    }
    product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
    product_load_started_at_.reset();
}

void TerrainApp::install_ready_product_build(const cubey::vulkan::Device& device,
                                             cubey::vulkan::GpuRuntime& gpu,
                                             cubey::vulkan::GpuSubmissionTicket retire_after) {
    if (!product_builds_.ready()) {
        return;
    }

    auto result = product_builds_.take_ready();
    install_product_build(device, gpu, retire_after, std::move(result.resident));
    product_rebuild_error_.clear();
}

void TerrainApp::poll_product_build(const cubey::vulkan::Device& device,
                                    cubey::vulkan::GpuRuntime& gpu,
                                    cubey::vulkan::GpuSubmissionTicket retire_after) {
    try {
        while (product_builds_.poll(gpu)) {
        }
        install_ready_product_build(device, gpu, retire_after);
        if (!product_builds_.busy() && !product_builds_.ready() &&
            product_builds_.status().phase == cubey::StagedResourcePhase::Failed) {
            product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
            product_load_started_at_.reset();
        }
    } catch (const std::exception& error) {
        product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
        product_load_started_at_.reset();
        product_rebuild_error_ = error.what();
    } catch (...) {
        product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
        product_load_started_at_.reset();
        product_rebuild_error_ = "unknown terrain product rebuild error";
    }
}

void TerrainApp::poll_atmosphere_atlases(const cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu,
                                         const cubey::vulkan::FrameResources& frame_resources,
                                         cubey::vulkan::GpuSubmissionTicket retire_after) {
    if (atmosphere_atlases_.poll(gpu, retire_after)) {
        for (std::uint32_t index = 0U; index < frame_resources.frame_slot_count(); ++index) {
            frame_resources.wait_for_frame(index);
            gpu.mark_submission_completed(frame_resources.submitted_ticket(index));
        }
        atmosphere_background_.update_texture_bindings(device, atmosphere_atlases_.bindings());
    }
}

void TerrainApp::finish_atmosphere_atlases(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu) {
    if (atmosphere_atlases_.finish(gpu)) {
        atmosphere_background_.update_texture_bindings(device, atmosphere_atlases_.bindings());
    }
}

void TerrainApp::create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                                   cubey::vulkan::GpuRuntime& gpu,
                                                   std::uint32_t frame_slot_count) {
    if (global_resources_created_) {
        return;
    }
    gpu_ = &gpu;
    gpu_profiler_.emplace(device, frame_slot_count, kTerrainGpuProfilerPassCapacity);
    const auto stage_proxy_data = terrain_stage_proxy_mesh_data();
    stage_proxy_mesh_.emplace(gpu, stage_proxy_data.mesh_config());
    terrain_runtime_.create(device, {
                                        .shaders = cubey::terrain_backdrop_runtime_shader_files(
                                            CUBEY_TERRAIN_SHADER_DIR),
                                        .frame_slot_count = frame_slot_count,
                                    });
    atmosphere_atlases_.create(device, gpu, {.night_sky_extent = 64U});
    atmosphere_background_.create_materials(
        device,
        {.frame_slot_count = frame_slot_count, .textures = atmosphere_atlases_.bindings()});
    cloud_runtime_.create_surface_resources(device, gpu, cloud_runtime_shader_files().generated,
                                            clouds_config_);
    hdr_post_frame_.create_materials(device, {.frame_slot_count = frame_slot_count});
    global_resources_created_ = true;
}

void TerrainApp::create_swapchain_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                            VkFormat color_format, std::uint32_t frame_slot_count) {
    capture_color_format_ = color_format;
    if (capture_runtime_) {
        capture_profile_slots_.assign(frame_slot_count, false);
        capture_runtime_->ensure_resources(device, *gpu_, frame_slot_count,
                                           {.extent = extent, .format = color_format});
    }
    scene_depth_.emplace(device, extent, true);
    terrain_target_info_ = cubey::TerrainBackdropRuntimeTargetInfo{
        .extent = extent,
        .color_format = kTerrainSceneColorFormat,
        .depth_format = scene_depth_->format(),
    };
    if (terrain_runtime_.product_ready()) {
        terrain_runtime_.create_target_resources(device, terrain_target_info_.value());
    }

    const std::array stage_proxy_shaders{
        cubey::render::vertex_shader_file(shader_path("terrain_stage_proxy.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("terrain_stage_proxy.frag.spv")),
    };
    const cubey::render::VertexInputLayout stage_proxy_vertex_input =
        cubey::render::vertex_position_color_normal_input_layout();
    const std::array stage_proxy_descriptor_set_layouts{terrain_runtime_.environment_layout()};
    stage_proxy_pipeline_.emplace(
        device, cubey::render::GraphicsPipelineFileResourceConfig{
                    .extent = extent,
                    .color_format = kTerrainSceneColorFormat,
                    .depth_format = scene_depth_->format(),
                    .shader_stage_files = stage_proxy_shaders,
                    .vertex_bindings = stage_proxy_vertex_input.bindings(),
                    .vertex_attributes = stage_proxy_vertex_input.attribute_descriptions(),
                    .descriptor_set_layouts = stage_proxy_descriptor_set_layouts,
                    .material_pass = terrain_stage_proxy_pass_info(),
                });

    const std::array atmosphere_shaders{
        cubey::render::vertex_shader_file(shader_path("atmosphere.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("atmosphere.frag.spv")),
    };
    atmosphere_background_.create_pipeline(device, {.extent = extent,
                                                    .color_format = kTerrainSceneColorFormat,
                                                    .shader_stage_files = atmosphere_shaders});
    cloud_runtime_.create_surface_target_resources(
        device, cloud_runtime_shader_files(),
        cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth,
        kTerrainSceneColorFormat, extent, frame_slot_count);
    const std::array post_shaders{
        cubey::render::vertex_shader_file(shader_path("forward_pbr_post.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("forward_pbr_post.frag.spv")),
    };
    hdr_post_frame_.create_pipeline(
        device,
        {.extent = extent, .color_format = color_format, .shader_stage_files = post_shaders});
    graph_executor_.clear();
    graph_executor_.resize(frame_slot_count);
}

void TerrainApp::prepare_capture_context(const cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu) {
    if (!capture_active_ || !config_.captured_water_context || !capture_runtime_ ||
        !terrain_target_info_)
        return;
    const bool first = capture_runtime_->context_product() == nullptr;
    capture_runtime_->prepare_context();
    capture_runtime_->ensure_resources(
        device, gpu, std::uint32_t(capture_profile_slots_.size()),
        {.extent = terrain_target_info_->extent, .format = capture_color_format_});
    if (first)
        configure_capture_camera();
}

void TerrainApp::destroy_swapchain_resources() {
    if (capture_runtime_)
        capture_runtime_->destroy_target_resources();
    graph_executor_.clear();
    hdr_post_frame_.destroy_pipeline();
    cloud_runtime_.destroy_surface_target_resources();
    atmosphere_background_.destroy_pipeline();
    stage_proxy_pipeline_.reset();
    terrain_runtime_.destroy_target_resources();
    terrain_target_info_.reset();
    scene_depth_.reset();
}

void TerrainApp::destroy_all_resources(cubey::vulkan::GpuRuntime& gpu) {
    landscape_builds_.shutdown(gpu);
    product_builds_.shutdown(gpu);
    if (capture_runtime_ && config_.captured_water_audit)
        capture_runtime_->verify_immutable_fields(gpu);
    destroy_swapchain_resources();
    if (capture_runtime_)
        capture_runtime_->destroy();
    hdr_post_frame_.destroy();
    cloud_runtime_.destroy();
    atmosphere_background_.destroy();
    atmosphere_atlases_.shutdown(gpu);
    terrain_runtime_.destroy();
    stage_proxy_mesh_.reset();
    gpu_profiler_.reset();
    global_resources_created_ = false;
}

[[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>&
TerrainApp::latest_gpu_timings() const {
    if (!gpu_profiler_.has_value()) {
        static const std::vector<cubey::vulkan::GpuPassTiming> empty;
        return empty;
    }
    return gpu_profiler_->latest_timings();
}

[[nodiscard]] cubey::vulkan::GpuTimestampProfiler* TerrainApp::gpu_profiler() {
    return gpu_profiler_.has_value() ? &gpu_profiler_.value() : nullptr;
}

void TerrainApp::collect_gpu_timings(cubey::profiling::ProfileRecorder* profile_recorder,
                                     std::uint64_t frame_index,
                                     cubey::render::FrameSlot frame_slot) {
    if (capture_runtime_ && capture_profile_slots_.at(frame_slot.index)) {
        record_gpu_timings(profile_recorder,
                           collected_profile_frame_index(frame_index, frame_slot),
                           capture_runtime_->collect_timings(frame_slot.index));
        return;
    }
    cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler();
    if (profiler == nullptr) {
        return;
    }
    profiler->collect(frame_slot.index);
    const std::uint64_t collected_frame =
        collected_profile_frame_index(frame_index, frame_slot);
    record_gpu_timings(profile_recorder, collected_frame, latest_gpu_timings());
    record_metrics(profile_recorder, collected_frame);
}

[[nodiscard]] std::uint64_t TerrainApp::shadow_caster_triangle_count() const noexcept {
    return terrain_runtime_.shadow_caster_triangle_count();
}
} // namespace cubey::projects::terrain::detail
