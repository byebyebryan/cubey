#include "terrain_app_internal.h"

namespace cubey::projects::terrain::detail {

bool same_source_path(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    return std::filesystem::absolute(lhs).lexically_normal() ==
           std::filesystem::absolute(rhs).lexically_normal();
}

std::vector<TerrainSourceChoice> terrain_source_choices(const TerrainRuntimeConfig& startup_config,
                                                        std::string& preset_catalog_error) {
    std::vector<TerrainSourceChoice> result{
        TerrainSourceChoice{
            .id = "startup",
            .label = "Startup source",
            .paths =
                {
                    .heightfield = startup_config.heightfield_path,
                    .surface_fields = startup_config.surface_fields_path,
                },
            .expected_seed = startup_config.expected_seed,
        },
    };

    std::optional<TerrainSourcePresetCatalog> preset_catalog;
    try {
        preset_catalog = load_terrain_source_preset_catalog(
            std::filesystem::path(CUBEY_TERRAIN_SOURCE_PRESET_CATALOG),
            std::filesystem::path(CUBEY_TERRAIN_OPTIONAL_PRESET_ASSETS));
        const TerrainSourcePaths default_paths{
            .heightfield = std::filesystem::path(CUBEY_TERRAIN_DEFAULT_HEIGHTFIELD),
            .surface_fields = std::filesystem::path(CUBEY_TERRAIN_DEFAULT_SURFACE_FIELDS),
        };
        if (same_source_path(startup_config.heightfield_path, default_paths.heightfield)) {
            result.front().id = "preset/" + preset_catalog->default_id;
            result.front().label = "Preset: " + preset_catalog->default_label;
            result.front().expected_seed = preset_catalog->default_seed;
        } else {
            result.push_back({
                .id = "preset/" + preset_catalog->default_id,
                .label = "Preset: " + preset_catalog->default_label,
                .paths = default_paths,
                .expected_seed = preset_catalog->default_seed,
            });
        }
    } catch (const std::exception& catalog_error) {
        preset_catalog_error = catalog_error.what();
    }

    constexpr std::array calibration_sources{
        std::pair{"hot-dry", "Calibration: Hot / dry"},
        std::pair{"hot-wet", "Calibration: Hot / wet"},
        std::pair{"cool-wet", "Calibration: Cool / wet"},
        std::pair{"cold-dry", "Calibration: Cold / dry"},
        std::pair{"cold-wet", "Calibration: Cold / wet"},
    };
    const std::filesystem::path calibration_root(CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSETS);
    for (const auto& [id, label] : calibration_sources) {
        const std::filesystem::path directory = calibration_root / id;
        result.push_back({
            .id = "calibration/" + std::string(id),
            .label = label,
            .paths = {.heightfield = directory, .surface_fields = directory},
            .expected_seed = 0U,
            .climate_required = true,
        });
    }

    if (preset_catalog.has_value()) {
        for (TerrainSourceCatalogEntry entry : preset_catalog->optional_presets) {
            TerrainSourceChoice choice{
                .id = "preset/" + entry.id,
                .label = "Preset: " + entry.label,
                .paths =
                    {
                        .heightfield = entry.heightfield_path,
                        .surface_fields = entry.surface_fields_path,
                    },
                .expected_seed = entry.seed,
                .climate_required = true,
                .preset = std::move(entry),
            };
            if (same_source_path(startup_config.heightfield_path, choice.paths.heightfield)) {
                result.front() = std::move(choice);
            } else {
                result.push_back(std::move(choice));
            }
        }
    }
    return result;
}

bool terrain_manifest_available(const std::filesystem::path& path, std::string_view manifest) {
    std::error_code error;
    const std::filesystem::path candidate =
        std::filesystem::is_directory(path, error) ? path / manifest : path;
    return !error && std::filesystem::is_regular_file(candidate, error) && !error;
}

bool terrain_source_climate_available(const TerrainSourcePaths& paths) {
    return terrain_manifest_available(paths.surface_fields, "surface-fields.json");
}

TerrainSourceAvailability terrain_source_choice_availability(const TerrainSourceChoice& choice) {
    if (choice.preset.has_value()) {
        return terrain_source_availability(*choice.preset);
    }

    const bool heightfield =
        terrain_manifest_available(choice.paths.heightfield, "heightfield.json");
    const bool surface_fields =
        !choice.climate_required || terrain_source_climate_available(choice.paths);
    if (heightfield && surface_fields) {
        return TerrainSourceAvailability::Available;
    }

    std::error_code heightfield_error;
    const bool heightfield_storage =
        std::filesystem::exists(choice.paths.heightfield, heightfield_error) && !heightfield_error;
    std::error_code surface_fields_error;
    const bool surface_fields_storage =
        choice.climate_required &&
        std::filesystem::exists(choice.paths.surface_fields, surface_fields_error) &&
        !surface_fields_error;
    return !heightfield_storage && !surface_fields_storage ? TerrainSourceAvailability::NotGenerated
                                                           : TerrainSourceAvailability::Incomplete;
}

std::string terrain_source_choice_label(const TerrainSourceChoice& choice,
                                        TerrainSourceAvailability availability) {
    if (availability == TerrainSourceAvailability::Available) {
        return choice.label;
    }
    return choice.label + " (" + std::string(terrain_source_availability_name(availability)) + ")";
}

std::uint64_t profile_frame_index(std::uint64_t frame_index) {
    return frame_index == 0U ? 0U : frame_index - 1U;
}

double elapsed_milliseconds(Clock::time_point started) {
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

std::uint64_t collected_profile_frame_index(std::uint64_t frame_index,
                                            cubey::render::FrameSlot frame_slot) {
    if (frame_index > frame_slot.count) {
        return frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return profile_frame_index(frame_index);
}

std::uint32_t sha256_word(std::string_view hash, std::size_t offset) noexcept {
    if (offset + 8U > hash.size()) {
        return 0U;
    }
    std::uint32_t value = 0U;
    for (std::size_t index = offset; index < offset + 8U; ++index) {
        const char digit = hash[index];
        const std::uint32_t nibble = digit >= '0' && digit <= '9'
                                         ? static_cast<std::uint32_t>(digit - '0')
                                         : static_cast<std::uint32_t>(digit - 'a' + 10);
        value = (value << 4U) | nibble;
    }
    return value;
}

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings) {
    if (recorder == nullptr) {
        return;
    }
    for (const cubey::vulkan::GpuPassTiming& timing : timings) {
        recorder->record_gpu_span(frame_index, timing.label, timing.milliseconds);
    }
}

void draw_terrain_gpu_timings(std::span<const cubey::vulkan::GpuPassTiming> timings) {
    ImGui::SeparatorText("GPU timings");
    for (const std::string_view label : kTerrainGpuTimingLabels) {
        const auto timing = std::find_if(timings.begin(), timings.end(),
                                         [label](const cubey::vulkan::GpuPassTiming& candidate) {
                                             return candidate.label == label;
                                         });
        if (timing == timings.end()) {
            ImGui::Text("%.*s: --", static_cast<int>(label.size()), label.data());
        } else {
            ImGui::Text("%s: %.3f ms", timing->label.c_str(), timing->milliseconds);
        }
    }
}

std::filesystem::path shader_path(std::filesystem::path filename) {
    return std::filesystem::path(CUBEY_TERRAIN_SHADER_DIR) / std::move(filename);
}

cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files() {
    return cubey::render::cloud_layer_runtime_shader_files(
        CUBEY_TERRAIN_SHADER_DIR,
        cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
}

std::filesystem::path require_heightfield(const std::filesystem::path& heightfield_path) {
    std::error_code error;
    if (!std::filesystem::exists(heightfield_path, error) || error) {
        throw std::runtime_error(
            "terrain heightfield asset is missing: " + heightfield_path.string() +
            "\nGenerate the canonical asset with:\n  cmake --build --preset dev --target "
            "cubey_terrain_generate_default_asset");
    }
    return heightfield_path;
}

std::optional<TerrainRasterClimateSource>
load_climate_source(const TerrainRuntimeConfig& config, const TerrainRasterHeightSource& source) {
    if (config.surface_fields_path.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    if (!std::filesystem::exists(config.surface_fields_path, error) || error) {
        if (config.surface_model == TerrainSurfaceModel::ClimateTransition) {
            throw std::runtime_error(
                "terrain surface fields are missing: " + config.surface_fields_path.string() +
                "\nGenerate the canonical companion with:\n  cmake --build --preset dev --target "
                "cubey_terrain_generate_surface_study_asset");
        }
        return std::nullopt;
    }
    TerrainRasterClimateSource climate(config.surface_fields_path);
    validate_terrain_climate_binding(source, climate);
    return climate;
}

TerrainBackdropPlacementPlan make_placement_stage(const TerrainRasterHeightSource& source,
                                                  const TerrainRuntimeConfig& config) {
    if (config.expected_seed.has_value() &&
        config.expected_seed.value() != source.metadata().seed) {
        throw std::runtime_error("terrain seed does not match the heightfield manifest");
    }
    TerrainBackdropPlacementRequest request;
    request.mode = config.placement;
    request.sample_index = config.placement_index;
    return plan_terrain_backdrop_placement(source, source.bounds(), request);
}

std::uint64_t
terrain_product_placement_parameter_hash(const TerrainRuntimeConfig& config,
                                         const TerrainBackdropPlacementPlan& placement) {
    cubey::procedural::ProceduralHashBuilder hash;
    hash.append_string("terrain-product-placement-v1");
    hash.append_u32(static_cast<std::uint32_t>(config.placement));
    hash.append_u32(config.placement_index);
    hash.append_float32(placement.placement.source_focus_xz.x);
    hash.append_float32(placement.placement.source_focus_xz.y);
    return hash.value();
}

cubey::render::MaterialPassInfo terrain_stage_proxy_pass_info() {
    return {
        .label = "terrain.stage-proxy",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .push_constants =
            {
                VkPushConstantRange{
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    .offset = 0,
                    .size = sizeof(TerrainStageProxyPushConstants),
                },
            },
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>
terrain_stage_proxy_mesh_data() {
    const auto sphere = cubey::render::make_uv_sphere_position_color_normal_uv_mesh({
        .radius = 20.0F,
        .latitude_segments = 24U,
        .longitude_segments = 48U,
        .color = {0.52F, 0.55F, 0.58F},
    });
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal> result;
    result.vertices.reserve(sphere.vertices.size());
    for (const cubey::render::VertexPositionColorNormalUv& vertex : sphere.vertices) {
        result.vertices.push_back({
            .position = vertex.position,
            .color = vertex.color,
            .normal = vertex.normal,
        });
    }
    result.indices = sphere.indices;
    return result;
}

std::string_view short_revision(std::string_view revision) {
    return revision.substr(0U, std::min<std::size_t>(revision.size(), 12U));
}

TerrainBackdropPlacementPlan loading_placement_stage() {
    TerrainBackdropPlacementPlan placement;
    placement.stage.source_center_height_m = 0.0F;
    placement.stage.target_height_m = 500.0F;
    placement.stage.minimum_camera_clearance_m = kTerrainDefaultForegroundHeightM;
    placement.stage.orbit_min_radius_m = 25.0F;
    placement.stage.orbit_default_radius_m = 350.0F;
    placement.stage.orbit_max_radius_m = kTerrainInspectionMaximumOrbitRadiusM;
    placement.stage.orbit_default_elevation_radians = 0.12F;
    return placement;
}

TerrainProjectConfig resolve_terrain_app_config(TerrainProjectConfig config) {
    resolve_terrain_project_config(config, std::filesystem::path(CUBEY_TERRAIN_DEFAULT_HEIGHTFIELD),
                                   std::filesystem::path(CUBEY_TERRAIN_DEFAULT_SURFACE_FIELDS));
    return config;
}
} // namespace cubey::projects::terrain::detail
