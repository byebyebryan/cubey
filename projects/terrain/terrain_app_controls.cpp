#include "terrain_app_internal.h"

namespace cubey::projects::terrain::detail {

void TerrainApp::refresh_source_choice_availability() {
    for (std::size_t index = 0U; index < source_choices_.size(); ++index) {
        const TerrainSourceChoice& choice = source_choices_[index];
        const TerrainSourceAvailability availability =
            terrain_source_choice_availability(choice);
        source_choice_availability_[index] = availability;
        source_choice_climate_available_[index] =
            availability == TerrainSourceAvailability::Available &&
            terrain_source_climate_available(choice.paths);
    }
}

void TerrainApp::refresh_landscape_availability() {
    landscape_availability_.clear();
    if (!landscape_catalog_)
        return;
    for (const auto& preset : landscape_catalog_->presets)
        landscape_availability_.push_back(
            cubey::asset::probe_terrain_landscape(preset, config_.landscape_cache_root));
}

void TerrainApp::request_landscape(const std::string& id) {
    if (!landscape_catalog_ || !terrain_target_info_)
        throw std::runtime_error("landscape catalog or render target is not ready");
    if (landscape_builds_.busy() || product_builds_.busy())
        throw std::runtime_error("terrain or landscape Apply is already loading");
    const auto preset = cubey::asset::terrain_landscape_preset(*landscape_catalog_, id);
    const auto probe =
        cubey::asset::probe_terrain_landscape(preset, config_.landscape_cache_root);
    if (probe.availability != cubey::asset::TerrainLandscapeAvailability::Available)
        throw std::runtime_error(probe.reason);
    TerrainProjectConfig next = terrain_landscape_request_config(config_, id);
    const auto target = cubey::render::ColorTargetView{.extent = terrain_target_info_->extent,
                                                       .format = capture_color_format_};
    const auto slots = std::uint32_t(capture_profile_slots_.size());
    landscape_error_.clear();
    static_cast<void>(landscape_builds_.request(
        id,
        [next = std::move(next)]() mutable {
            next = resolve_terrain_app_config(std::move(next));
            LandscapeBuild build{
                .config = std::move(next), .runtime = {}, .observed_install_heap_bytes = 0};
            build.runtime = prepare_terrain_capture(build.config, CUBEY_TERRAIN_SHADER_DIR);
            return build;
        },
        [this, target, slots](cubey::vulkan::GpuOwnerContext& owner,
                              LandscapeBuild&& prepared) {
            prepared.runtime->ensure_resources(owner.device(), *gpu_, slots, target);
            const auto budget = owner.device().device_memory_budget();
            prepared.observed_install_heap_bytes =
                budget.available ? budget.device_local_usage : 0;
            return std::move(prepared);
        }));
}

void TerrainApp::poll_landscape_build(const cubey::vulkan::Device& device,
                                      cubey::vulkan::GpuRuntime& gpu,
                                      cubey::vulkan::GpuSubmissionTicket retire_after) {
    static_cast<void>(landscape_builds_.poll(gpu));
    const auto& status = landscape_builds_.status();
    if (status.phase == cubey::StagedResourcePhase::Failed)
        landscape_error_ = status.error;
    if (!landscape_builds_.ready() || !terrain_target_info_)
        return;
    auto ready = landscape_builds_.take_ready();
    auto& next = ready.resident;
    // A resize during preparation is allowed. Rebuild only the new resident's
    // targets before installing it; the old resident remains untouched.
    try {
        next.runtime->ensure_resources(
            device, gpu, std::uint32_t(capture_profile_slots_.size()),
            {.extent = terrain_target_info_->extent, .format = capture_color_format_});
    } catch (const std::exception& error) {
        landscape_error_ = error.what();
        return;
    }
    auto retired = std::make_shared<std::unique_ptr<cubey::TerrainWaterCaptureRuntime>>();
    *retired = std::move(capture_runtime_);
    try {
        gpu.defer_destruction_after(retire_after, [retired] { retired->reset(); });
    } catch (const std::exception& error) {
        capture_runtime_ = std::move(*retired);
        landscape_error_ = error.what();
        return;
    }
    capture_runtime_ = std::move(next.runtime);
    config_ = std::move(next.config);
    runtime_config_ = config_.runtime;
    capture_active_ = true;
    capture_animation_s_ = capture_review_s_ = 0;
    std::fill(capture_profile_slots_.begin(), capture_profile_slots_.end(), false);
    configure_capture_camera();
    for (std::size_t i = 0; i < landscape_catalog_->presets.size(); ++i)
        if (landscape_catalog_->presets[i].id == config_.active_landscape_id)
            edit_landscape_index_ = i;
    source_choices_ = terrain_source_choices(runtime_config_, preset_catalog_error_);
    source_choice_availability_.resize(source_choices_.size());
    source_choice_climate_available_.resize(source_choices_.size());
    refresh_source_choice_availability();
    source_choice_index_ = edit_source_choice_index_ = 0;
    backdrop_requested_ = false;
    std::printf("terrain_landscape_applied: id=%s prepare_ms=%.3f install_ms=%.3f water=%u "
                "deferred=%zu observed_install_heap_mib=%.3f\n",
                config_.active_landscape_id.c_str(), ready.prepare_milliseconds,
                ready.install_milliseconds, config_.site_water_binding_path ? 1U : 0U,
                gpu.deferred_destruction_count(),
                double(next.observed_install_heap_bytes) / (1024.0 * 1024.0));
    log_landscape_presentation();
}

void TerrainApp::log_landscape_presentation() const {
    if (!capture_runtime_)
        return;
    const auto summary = terrain_landscape_presentation(config_, capture_runtime_->capture());
    std::printf("terrain_landscape_summary: %s; showing=%s; %s; %s; history=%s\n",
                summary.mode.c_str(), summary.title.c_str(), summary.water_coverage.c_str(),
                summary.terrain_coverage.c_str(), summary.provenance.c_str());
}

void TerrainApp::draw_landscape_selector() {
    ImGui::SeparatorText("Landscape");
    if (!config_.landscape_startup_notice.empty())
        ImGui::TextWrapped("%s", config_.landscape_startup_notice.c_str());
    const auto showing =
        capture_active_ && capture_runtime_
            ? terrain_landscape_presentation(config_, capture_runtime_->capture()).title
            : std::string("Legacy backdrop");
    ImGui::TextWrapped("Showing: %s", showing.c_str());
    if (!landscape_catalog_) {
        ImGui::TextWrapped("Catalog unavailable: %s", landscape_error_.c_str());
        return;
    }
    const auto& presets = landscape_catalog_->presets;
    const bool busy = landscape_builds_.busy() || product_builds_.busy();
    ImGui::BeginDisabled(busy);
    if (ImGui::BeginCombo("Site", presets.at(edit_landscape_index_).label.c_str())) {
        std::string previous;
        for (std::size_t i = 0; i < presets.size(); ++i) {
            if (presets[i].site_id == previous)
                continue;
            previous = presets[i].site_id;
            if (ImGui::Selectable(presets[i].label.c_str(),
                                  presets[i].site_id == presets[edit_landscape_index_].site_id))
                edit_landscape_index_ = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Water state",
                          presets.at(edit_landscape_index_).water_label.c_str())) {
        for (std::size_t i = 0; i < presets.size(); ++i) {
            if (presets[i].site_id != presets[edit_landscape_index_].site_id)
                continue;
            const auto& probe = landscape_availability_.at(i);
            const auto label = presets[i].water_label + " (" +
                               std::string(cubey::asset::terrain_landscape_availability_name(
                                   probe.availability)) +
                               ")";
            if (ImGui::Selectable(label.c_str(), i == edit_landscape_index_))
                edit_landscape_index_ = i;
        }
        ImGui::EndCombo();
    }
    const auto& selected = presets.at(edit_landscape_index_);
    const auto& probe = landscape_availability_.at(edit_landscape_index_);
    ImGui::TextWrapped("Selected: %s", terrain_landscape_selection_label(selected).c_str());
    if (probe.availability != cubey::asset::TerrainLandscapeAvailability::Available)
        ImGui::TextWrapped("Unavailable: %s", probe.reason.c_str());
    else if (selected.id != config_.active_landscape_id)
        ImGui::TextDisabled("Ready. Apply loads this selection and resets the camera.");
    ImGui::BeginDisabled(probe.availability !=
                             cubey::asset::TerrainLandscapeAvailability::Available ||
                         selected.id == config_.active_landscape_id);
    if (ImGui::Button("Apply landscape")) {
        try {
            request_landscape(selected.id);
        } catch (const std::exception& error) {
            landscape_error_ = error.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh availability"))
        refresh_landscape_availability();
    ImGui::EndDisabled();
    const auto& status = landscape_builds_.status();
    if (landscape_builds_.busy()) {
        const auto& loading = cubey::asset::terrain_landscape_preset(*landscape_catalog_,
                                                                     status.generation.label);
        ImGui::TextWrapped("Loading: %s (%s)",
                           terrain_landscape_selection_label(loading).c_str(),
                           cubey::staged_resource_phase_name(status.phase).data());
        ImGui::TextWrapped(
            "Current landscape is kept until ready. Upload may briefly pause rendering.");
    } else if (product_builds_.busy())
        ImGui::TextWrapped("Loading legacy terrain; current landscape is kept until ready.");
    if (!landscape_error_.empty())
        ImGui::TextWrapped("Last load failed; previous landscape kept: %s",
                           landscape_error_.c_str());
    ImGui::TextWrapped(
        "Scene changes only after Apply. No solver or terrain generator is started.");
    if (ImGui::TreeNode("Loading diagnostics")) {
        ImGui::Text("Selected ID: %s", selected.id.c_str());
        ImGui::Text("Showing ID: %s", config_.active_landscape_id.c_str());
        ImGui::Text("Last prepare / upload: %.1f / %.1f ms", status.prepare_milliseconds,
                    status.install_milliseconds);
        ImGui::TreePop();
    }
}

void TerrainApp::draw_ui(cubey::host::WindowedAppContext& context) {
    if (!cubey::host::begin_control_panel("Terrain", {.width = 410.0F})) {
        ImGui::End();
        return;
    }

    draw_landscape_selector();

    if (capture_runtime_) {
        int mode = capture_active_ ? 1 : 0;
        if (ImGui::Combo("View", &mode, "Legacy backdrop\0Prepared landscape\0")) {
            capture_active_ = mode == 1;
            if (!capture_active_ && !backdrop_requested_)
                request_product_build(runtime_config_, source_choice_index_);
        }
        if (capture_active_) {
            const auto& capture = capture_runtime_->capture();
            const auto summary = terrain_landscape_presentation(config_, capture);
            ImGui::SeparatorText(summary.mode.c_str());
            ImGui::TextWrapped("%s", summary.water_coverage.c_str());
            ImGui::TextWrapped("%s", summary.terrain_coverage.c_str());
            ImGui::TextWrapped("%s", summary.provenance.c_str());
            if (summary.has_water)
                ImGui::TextWrapped("Rain is capture history, not current weather. Rainfall "
                                   "cannot be edited here.");
            if (ImGui::TreeNode("Asset / rendering diagnostics")) {
                if (config_.terrain_site)
                    ImGui::Text("Site: %s / world %.12s", config_.terrain_site->id.c_str(),
                                config_.terrain_site->world_id.c_str());
                ImGui::Text("Grid: %u x %u", capture.grid.width, capture.grid.height);
                ImGui::Text("Source: %s", capture_runtime_->source_label().c_str());
                ImGui::Text("Elevation hash: %.12s", capture.source_elevation_sha256.c_str());
                ImGui::Text("Footprint: %.3f x %.3f km (cell centers)",
                            float(capture.grid.width - 1U) * capture.grid.cell_size_m * 0.001F,
                            float(capture.grid.height - 1U) * capture.grid.cell_size_m *
                                0.001F);
                ImGui::Text("Source origin: %.0f, %.0f m", capture.grid.sample_origin_xz_m.x,
                            capture.grid.sample_origin_xz_m.y);
                ImGui::Text("Render detail: %s", config_.captured_water_detail.c_str());
                if (config_.captured_water_detail != "uniform")
                    ImGui::TextDisabled(
                        "Fixed fine region %.2f km; no camera-following LOD. Far "
                        "shadows unqualified.",
                        config_.captured_water_detail_span_m * .001F);
                ImGui::TreePop();
            }
            ImGui::SeparatorText("Explore landscape");
            int view = config_.water_capture_camera == "overview"    ? 0
                       : config_.water_capture_camera == "stream"    ? 1
                       : config_.water_capture_camera == "lake"      ? 2
                       : config_.water_capture_camera == "landscape" ? 3
                                                                     : 4;
            if (config_.terrain_site) {
                int framing = config_.captured_water_span_m.value_or(11457.6F) > 20000 ? 1 : 0;
                if (ImGui::Combo("Framing", &framing, "Local detail\0Wider landscape\0")) {
                    config_.water_capture_camera = "landscape";
                    config_.captured_water_span_m = framing == 0 ? 11457.6F : 45000.F;
                    configure_capture_camera();
                }
            } else if (ImGui::Combo("Camera", &view,
                                    "Overview\0Stream\0Lake\0Landscape\0Boundary audit\0")) {
                config_.water_capture_camera = view == 0   ? "overview"
                                               : view == 1 ? "stream"
                                               : view == 2 ? "lake"
                                               : view == 3 ? "landscape"
                                                           : "boundary";
                configure_capture_camera();
            }
            ImGui::BeginDisabled(config_.terrain_site && !config_.site_water_binding_path);
            ImGui::Checkbox("Show water", &config_.captured_water_visible);
            ImGui::Checkbox("Animate saved flow cues", &config_.captured_water_animate);
            ImGui::EndDisabled();
            if (summary.has_water)
                ImGui::TextWrapped("Animation changes surface appearance only; water levels "
                                   "and distribution stay fixed.");
            if (ImGui::Button("Reset landscape camera"))
                configure_capture_camera();
            if (ImGui::TreeNode("Appearance & coverage options")) {
                int readability = config_.terrain_readability == "filtered-study" ? 3
                                  : config_.terrain_readability == "refined"      ? 2
                                  : config_.terrain_readability == "detail-off"   ? 1
                                                                                  : 0;
                if (ImGui::Combo("Terrain material", &readability,
                                 "Previous reference\0Periodic detail off\0Refined "
                                 "(default)\0Filtered lighting (study)\0")) {
                    config_.terrain_readability = readability == 3   ? "filtered-study"
                                                  : readability == 2 ? "refined"
                                                  : readability == 1 ? "detail-off"
                                                                     : "current";
                    capture_runtime_->set_terrain_readability(unsigned(readability));
                }
                int ground_view = config_.captured_water_ground_view == "shaded"       ? 0
                                  : config_.captured_water_ground_view == "normal"     ? 1
                                  : config_.captured_water_ground_view == "clay"       ? 2
                                  : config_.captured_water_ground_view == "ownership"  ? 3
                                  : config_.captured_water_ground_view == "clay-clean" ? 4
                                                                                       : 5;
                if (ImGui::Combo("Ground diagnostic (hides water)", &ground_view,
                                 "Shaded\0Normals\0Clay\0Ownership\0Clean clay\0Albedo\0"))
                    config_.captured_water_ground_view = ground_view == 0   ? "shaded"
                                                         : ground_view == 1 ? "normal"
                                                         : ground_view == 2 ? "clay"
                                                         : ground_view == 3 ? "ownership"
                                                         : ground_view == 4 ? "clay-clean"
                                                                            : "albedo";
                if (ImGui::Checkbox("Wider ground context (no added water)",
                                    &config_.captured_water_context))
                    configure_capture_camera();
                if (config_.captured_water_context) {
                    ImGui::TextWrapped(
                        "Water stops at its recorded rectangle. Exterior ground is "
                        "uncomputed water coverage, not proven dry terrain.");
                    ImGui::TextDisabled(
                        "Core-focused 4096 shadow; far-context shadows unqualified.");
                }
                ImGui::TextDisabled("Clear sky only; surface-view clouds are excluded from "
                                    "this elevated view.");
                ImGui::TextDisabled("No radial terrain or radial shadow casters in this view.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Environment (render only)")) {
                if (!capture_runtime_->environment()) {
                    if (ImGui::Button("Enable shared clear sky and day/night"))
                        capture_runtime_->enable_environment({config_.atmosphere});
                } else {
                    ImGui::Checkbox("Shared clear sky and day/night",
                                    &capture_runtime_->environment_enabled());
                    if (capture_runtime_->environment_enabled())
                        cubey::host::draw_terrain_surface_environment_controls(
                            *capture_runtime_->environment());
                }
                ImGui::TreePop();
            }
            ImGui::End();
            return;
        }
    }
    if (!capture_error_.empty())
        ImGui::TextWrapped("Captured patch disabled: %s", capture_error_.c_str());
    ImGui::SeparatorText("Source");
    refresh_source_choice_availability();
    const cubey::StagedResourceStatus& build_status = product_builds_.status();
    const bool product_build_pending = product_builds_.busy();
    const std::string source_preview =
        terrain_source_choice_label(source_choices_.at(edit_source_choice_index_),
                                    source_choice_availability_.at(edit_source_choice_index_));
    ImGui::BeginDisabled(product_build_pending);
    if (ImGui::BeginCombo("Terrain source", source_preview.c_str())) {
        for (std::size_t index = 0U; index < source_choices_.size(); ++index) {
            const TerrainSourceChoice& choice = source_choices_[index];
            const TerrainSourceAvailability availability = source_choice_availability_[index];
            const bool available = availability == TerrainSourceAvailability::Available;
            const bool selected = index == edit_source_choice_index_;
            const std::string label = terrain_source_choice_label(choice, availability);
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(label.c_str(), selected)) {
                edit_source_choice_index_ = index;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (source_.has_value()) {
        ImGui::Text("Showing: %s", source_choices_.at(source_choice_index_).label.c_str());
    } else {
        ImGui::TextDisabled("Showing: loading placeholder");
    }
    if (product_build_pending) {
        const TerrainProductLoadPhase load_phase =
            product_load_phase_.load(std::memory_order_relaxed);
        const std::string_view phase = terrain_product_load_phase_name(load_phase);
        ImGui::Text("Loading: %s", build_status.generation.label.c_str());
        if (product_load_started_at_.has_value()) {
            ImGui::Text("Phase: %.*s (%.1f s)", static_cast<int>(phase.size()), phase.data(),
                        elapsed_milliseconds(*product_load_started_at_) * 0.001);
        } else {
            ImGui::Text("Phase: %.*s", static_cast<int>(phase.size()), phase.data());
        }
        if (source_.has_value()) {
            ImGui::TextDisabled("Showing current terrain until replacement is ready");
        }
    }
    if (source_.has_value()) {
        const TerrainRasterProvenance& provenance = source_->provenance();
        const TerrainHeightSourceMetadata metadata = source_->metadata();
        ImGui::Text("%.*s, seed %llu", static_cast<int>(metadata.id.size()), metadata.id.data(),
                    static_cast<unsigned long long>(metadata.seed));
        ImGui::Text("%u x %u at %.0f m", source_->width(), source_->height(),
                    source_->sample_spacing_m());
        const float width_km =
            static_cast<float>(source_->width() - 1U) * source_->sample_spacing_m() * 0.001F;
        const float height_km =
            static_cast<float>(source_->height() - 1U) * source_->sample_spacing_m() * 0.001F;
        ImGui::Text("Coverage: %.2f x %.2f km", width_km, height_km);
        if (!provenance.generator.empty()) {
            ImGui::Text("Generator: %s", provenance.generator.c_str());
        }
        if (!provenance.model_id.empty()) {
            ImGui::TextWrapped("Model: %s", provenance.model_id.c_str());
        }
        if (!provenance.code_revision.empty() || !provenance.model_revision.empty()) {
            const std::string_view code = short_revision(provenance.code_revision);
            const std::string_view model = short_revision(provenance.model_revision);
            ImGui::Text("Revisions: %.*s / %.*s", static_cast<int>(code.size()), code.data(),
                        static_cast<int>(model.size()), model.data());
        }
        const std::string manifest = provenance.manifest_path.string();
        ImGui::TextWrapped("Manifest: %s", manifest.c_str());
    } else {
        ImGui::TextDisabled("No resident terrain source");
    }
    if (!preset_catalog_error_.empty()) {
        ImGui::TextWrapped("Preset catalog: %s", preset_catalog_error_.c_str());
    }

    ImGui::SeparatorText("Placement");
    ImGui::BeginDisabled(product_build_pending);
    if (ImGui::RadioButton("Selected",
                           edit_placement_mode_ == TerrainPlacementMode::Selected)) {
        edit_placement_mode_ = TerrainPlacementMode::Selected;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Raw center",
                           edit_placement_mode_ == TerrainPlacementMode::RawCenter)) {
        edit_placement_mode_ = TerrainPlacementMode::RawCenter;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Raw sample",
                           edit_placement_mode_ == TerrainPlacementMode::RawSample)) {
        edit_placement_mode_ = TerrainPlacementMode::RawSample;
    }
    if (edit_placement_mode_ == TerrainPlacementMode::RawSample) {
        constexpr std::uint32_t step = 1U;
        constexpr std::uint32_t fast_step = 10U;
        ImGui::InputScalar("Sample index", ImGuiDataType_U32, &edit_placement_index_, &step,
                           &fast_step);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Surface study");
    if (ImGui::RadioButton("Mineral control",
                           edit_surface_model_ == TerrainSurfaceModel::MineralControl)) {
        edit_surface_model_ = TerrainSurfaceModel::MineralControl;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Landform",
                           edit_surface_model_ == TerrainSurfaceModel::LandformTransition)) {
        edit_surface_model_ = TerrainSurfaceModel::LandformTransition;
    }
    ImGui::BeginDisabled(!source_choice_climate_available_[edit_source_choice_index_]);
    if (ImGui::RadioButton("Climate",
                           edit_surface_model_ == TerrainSurfaceModel::ClimateTransition)) {
        edit_surface_model_ = TerrainSurfaceModel::ClimateTransition;
    }
    ImGui::EndDisabled();
    if (ImGui::RadioButton("Source-aligned masks (study)",
                           edit_surface_model_ == TerrainSurfaceModel::CorrelatedSurface)) {
        edit_surface_model_ = TerrainSurfaceModel::CorrelatedSurface;
    }
    const std::string_view displayed_surface =
        terrain_surface_model_name(runtime_config_.surface_model);
    ImGui::Text("Active surface: %.*s", static_cast<int>(displayed_surface.size()),
                displayed_surface.data());
    if (climate_source_.has_value()) {
        const TerrainRasterClimateMetadata& climate = climate_source_->metadata();
        ImGui::Text("Climate grid: %u x %u at %.0f m", climate.width, climate.height,
                    climate.sample_spacing_m);
        ImGui::Text("Climate hash: %.*s", 12, climate.climate_sha256.c_str());
    } else {
        ImGui::TextDisabled("Climate companion unavailable");
    }

    const bool product_edit_dirty = edit_source_choice_index_ != source_choice_index_ ||
                                    edit_placement_mode_ != placement_stage_.mode ||
                                    edit_placement_index_ != placement_stage_.sample_index ||
                                    edit_surface_model_ != runtime_config_.surface_model;
    const bool edited_source_available =
        source_choice_availability_[edit_source_choice_index_] ==
        TerrainSourceAvailability::Available;
    ImGui::BeginDisabled(!source_.has_value() || !product_edit_dirty || product_build_pending ||
                         !edited_source_available);
    if (ImGui::Button("Apply terrain")) {
        start_product_build();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!source_.has_value() || !product_edit_dirty || product_build_pending);
    if (ImGui::Button("Revert")) {
        edit_source_choice_index_ = source_choice_index_;
        edit_placement_mode_ = placement_stage_.mode;
        edit_placement_index_ = placement_stage_.sample_index;
        edit_surface_model_ = runtime_config_.surface_model;
    }
    ImGui::EndDisabled();

    if (build_status.prepare_milliseconds > 0.0 || build_status.install_milliseconds > 0.0) {
        ImGui::Text("Prepare / install: %.1f / %.1f ms", build_status.prepare_milliseconds,
                    build_status.install_milliseconds);
    }
    if (!build_status.error.empty()) {
        ImGui::TextWrapped("Terrain error: %s", build_status.error.c_str());
    }
    if (!product_rebuild_error_.empty()) {
        ImGui::TextWrapped("Terrain activation error: %s", product_rebuild_error_.c_str());
    }
    if (source_.has_value()) {
        const std::string_view preparation_source =
            terrain_product_preparation_source_name(product_cache_diagnostics_.source);
        const std::string_view lookup =
            terrain_cache_lookup_name(product_cache_diagnostics_.lookup);
        ImGui::Text("Source / climate / placement: %.1f / %.1f / %.1f ms",
                    source_load_milliseconds_, climate_load_milliseconds_,
                    placement_milliseconds_);
        ImGui::Text("Product preparation: %.*s, lookup %.*s",
                    static_cast<int>(preparation_source.size()), preparation_source.data(),
                    static_cast<int>(lookup.size()), lookup.data());
        ImGui::Text("Cache load / decode: %.1f / %.1f ms",
                    product_cache_diagnostics_.load_milliseconds,
                    product_cache_diagnostics_.decode_milliseconds);
        ImGui::Text("Generate / encode / store: %.1f / %.1f / %.1f ms",
                    product_cache_diagnostics_.generation_milliseconds,
                    product_cache_diagnostics_.encode_milliseconds,
                    product_cache_diagnostics_.store_milliseconds);
        if (!product_cache_diagnostics_.diagnostic.empty()) {
            ImGui::TextWrapped("Cache diagnostic: %s",
                               product_cache_diagnostics_.diagnostic.c_str());
        }
    }

    const TerrainDirectionalPlacementPlan& placement = placement_stage_.placement;
    const std::string_view placement_name = terrain_placement_mode_name(placement_stage_.mode);
    ImGui::Text("Active: %.*s", static_cast<int>(placement_name.size()), placement_name.data());
    if (placement_stage_.mode == TerrainPlacementMode::RawSample) {
        ImGui::Text("Active sample: %u", placement_stage_.sample_index);
    }
    ImGui::Text("Focus: %.3f, %.3f km", placement.source_focus_xz.x * 0.001F,
                placement.source_focus_xz.y * 0.001F);
    ImGui::Text("Local stage: %s", placement.local_contract_satisfied ? "pass" : "fail");
    ImGui::Text("Mountain composition: %s",
                placement.contract_satisfied ? "preferred" : "best available");
    ImGui::Text("Score: %.3f", placement.score);
    ImGui::Text("Local relief (%.0f m): %.1f / %.1f m", placement.local_radius_m,
                placement.local_relief_m, placement.maximum_local_relief_m);
    ImGui::Text("P95 slope: %.3f / %.3f", placement.local_p95_slope,
                placement.maximum_local_p95_slope);
    ImGui::Text("Mountain/open sectors: %u / %u", placement.mountain_sector_count,
                placement.open_sector_count);
    ImGui::Text("Mountain/open arcs: %u / %u", placement.largest_mountain_arc_sectors,
                placement.largest_open_arc_sectors);
    ImGui::Text("Baked clearance: %.1f m", placement_stage_.stage.minimum_camera_clearance_m);

    ImGui::SeparatorText("Camera");
    float radius = orbit_controller_.distance();
    const TerrainBackdropStagePlan& stage = placement_stage_.stage;
    if (ImGui::SliderFloat("Orbit radius", &radius, stage.orbit_min_radius_m,
                           kTerrainInspectionMaximumOrbitRadiusM, "%.0f m",
                           ImGuiSliderFlags_Logarithmic)) {
        orbit_controller_.set_distance(radius);
    }
    float elevation_degrees =
        (stage.orbit_default_elevation_radians - orbit_controller_.pitch()) * kRadiansToDegrees;
    if (ImGui::DragFloat("Elevation", &elevation_degrees, 0.25F, 0.0F, 0.0F, "%.1f deg")) {
        orbit_controller_.set_pitch(stage.orbit_default_elevation_radians -
                                    elevation_degrees * kDegreesToRadians);
    }
    const float maximum_foreground_height_m =
        std::max(kTerrainMaximumForegroundHeightM, baked_foreground_height_m_);
    ImGui::SliderFloat("Foreground height", &foreground_height_m_,
                       kTerrainMinimumForegroundHeightM, maximum_foreground_height_m, "%.0f m",
                       ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Reset Camera")) {
        foreground_height_m_ = runtime_config_.initial_foreground_height_m;
        reset_inspection_camera();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Foreground sphere", &runtime_config_.foreground_sphere);

    ImGui::SeparatorText("Presentation");
    int material = static_cast<int>(runtime_config_.material);
    if (ImGui::RadioButton("Flat", material == static_cast<int>(TerrainMaterialMode::Flat))) {
        runtime_config_.material = TerrainMaterialMode::Flat;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Filtered detail (previous)",
                           material == static_cast<int>(TerrainMaterialMode::FilteredDetail))) {
        runtime_config_.material = TerrainMaterialMode::FilteredDetail;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Readable detail (default)",
                           material == static_cast<int>(TerrainMaterialMode::ReadableDetail)))
        runtime_config_.material = TerrainMaterialMode::ReadableDetail;
    ImGui::Checkbox("Directional shadows", &runtime_config_.shadows);
    ImGui::SliderFloat("Aerial perspective", &runtime_config_.aerial_perspective_strength, 0.0F,
                       1.0F, "%.2f");

    constexpr std::array diagnostics{
        TerrainDebugView::Surface,
        TerrainDebugView::Height,
        TerrainDebugView::Slope,
        TerrainDebugView::Clay,
        TerrainDebugView::Normal,
        TerrainDebugView::ClassificationNormal,
        TerrainDebugView::MaterialWeights,
        TerrainDebugView::Vegetation,
        TerrainDebugView::Moisture,
        TerrainDebugView::AmbientVisibility,
        TerrainDebugView::AmbientLighting,
        TerrainDebugView::DirectLighting,
        TerrainDebugView::MaterialAlbedo,
        TerrainDebugView::MaterialNormal,
        TerrainDebugView::MaterialRoughness,
        TerrainDebugView::SunVisibility,
        TerrainDebugView::ProjectedEdge,
        TerrainDebugView::StageOwnership,
    };
    const std::string_view current_view = terrain_debug_view_name(runtime_config_.debug_view);
    if (ImGui::BeginCombo("Diagnostic", current_view.data())) {
        for (const TerrainDebugView view : diagnostics) {
            const std::string_view name = terrain_debug_view_name(view);
            const bool selected = view == runtime_config_.debug_view;
            if (ImGui::Selectable(name.data(), selected)) {
                runtime_config_.debug_view = view;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Renderer");
    ImGui::Text("Render stride: %u", product_info_.request.render_stride);
    ImGui::Text(
        "Product / center triangles: %llu / %llu",
        static_cast<unsigned long long>(product_info_.diagnostics.render_triangle_count),
        static_cast<unsigned long long>(
            product_info_.diagnostics.center_render_triangle_count));
    ImGui::Text("Submitted sectors: %u", terrain_runtime_.draw_plan().submitted_sector_count);
    ImGui::Text("Submitted triangles: %u",
                terrain_runtime_.draw_plan().submitted_triangle_count);
    ImGui::Text("Cached source samples: %llu",
                static_cast<unsigned long long>(product_info_.diagnostics.source_sample_count));
    ImGui::Text("Sampled / render vertices: %llu / %llu",
                static_cast<unsigned long long>(product_info_.diagnostics.sampled_vertex_count),
                static_cast<unsigned long long>(product_info_.diagnostics.render_vertex_count));
    const double retained_vertex_ratio =
        product_info_.diagnostics.sampled_vertex_count == 0U
            ? 0.0
            : static_cast<double>(product_info_.diagnostics.render_vertex_count) /
                  static_cast<double>(product_info_.diagnostics.sampled_vertex_count);
    ImGui::Text("Vertex compaction: %.1f%% removed", (1.0 - retained_vertex_ratio) * 100.0);
    ImGui::Text("Mesh upload: %.2f MiB / %u transfer%s",
                static_cast<double>(terrain_runtime_.mesh_upload_bytes()) / (1024.0 * 1024.0),
                terrain_runtime_.mesh_upload_transfer_submissions(),
                terrain_runtime_.mesh_upload_transfer_submissions() == 1U ? "" : "s");
    ImGui::Text("Mean vegetation / moisture: %.3f / %.3f",
                product_info_.diagnostics.mean_vegetation,
                product_info_.diagnostics.mean_moisture);
    ImGui::Text("Shadow map: %u x %u, %s", cubey::render::kTerrainShadowMapExtent,
                cubey::render::kTerrainShadowMapExtent,
                terrain_runtime_.shadow_cache().valid ? "valid" : "pending");
    ImGui::Text("Shadow texel / depth span: %.1f m / %.0f m",
                terrain_runtime_.shadow_projection().texel_world_size_m,
                terrain_runtime_.shadow_projection().depth_span_m);
    ImGui::Text("Shadow casters: %llu triangles (outer backdrop)",
                static_cast<unsigned long long>(shadow_caster_triangle_count()));
    ImGui::Text("Shadow updates: %llu",
                static_cast<unsigned long long>(terrain_runtime_.shadow_cache().update_count));
    if (!terrain_runtime_.shadow_projection().light_above_horizon) {
        ImGui::Text("Shadow update suspended below horizon");
    }
    draw_terrain_gpu_timings(latest_gpu_timings());

    if (cubey::host::draw_atmosphere_environment_controls(atmosphere_state_,
                                                          {.default_open = true})) {
        cubey::atmosphere_environment_resolve_run_state(atmosphere_state_);
    }
    if (cubey::host::draw_cloud_environment_controls(
            clouds_config_,
            {.default_open = false,
             .help = "Shared Cloud V1 layer composited behind the terrain and foreground.",
             .enabled_help = "Composite clouds into the terrain environment.",
             .scene_depth_occlusion_enabled = true,
             .scene_depth_fade_m = kTerrainCloudSceneDepthFadeM})) {
        clouds_config_.layer.planet_radius_m =
            atmosphere_state_.environment.bottom_radius_km * 1000.0F;
        cloud_runtime_.set_config(clouds_config_);
        cloud_runtime_.update_weather_texture(context.device(), context.gpu(),
                                              cloud_runtime_shader_files().generated.weather);
    }
    ImGui::End();
}

void TerrainApp::configure_camera_for_placement(bool reset_foreground_height) {
    const TerrainBackdropStagePlan& stage = placement_stage_.stage;
    orbit_controller_.set_distance_limits(stage.orbit_min_radius_m,
                                          kTerrainInspectionMaximumOrbitRadiusM);
    orbit_controller_.set_home_distance(
        runtime_config_.initial_orbit_radius_m.value_or(stage.orbit_default_radius_m));
    baked_foreground_height_m_ = stage.target_height_m - stage.source_center_height_m;
    if (reset_foreground_height) {
        foreground_height_m_ = runtime_config_.initial_foreground_height_m;
    }
    reset_inspection_camera();
}

void TerrainApp::reset_inspection_camera() {
    const TerrainBackdropStagePlan& stage = placement_stage_.stage;
    orbit_controller_.reset();
    if (runtime_config_.initial_elevation_radians.has_value()) {
        orbit_controller_.set_pitch(stage.orbit_default_elevation_radians -
                                    runtime_config_.initial_elevation_radians.value());
    }
}
} // namespace cubey::projects::terrain::detail
