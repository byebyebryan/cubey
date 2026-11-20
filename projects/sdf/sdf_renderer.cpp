#include "sdf_renderer.h"
#include "sdf_render_modes.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph_builder.h>
#include <cubey/render/render_graph_resolve.h>
#include <cubey/vulkan/command_recorder.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace cubey::projects::sdf {
namespace {
struct Camera {
    math::Mat4 inverse_vp;
    math::Vec4 eye, sunlight, options, forward;
};
static_assert(sizeof(Camera) == 128);
constexpr auto hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
} // namespace
void Renderer::create(vulkan::Device& device, vulkan::GpuRuntime& gpu, VkExtent2D extent,
                      VkFormat format, std::uint32_t slots, const Field* field,
                      const SdfConfig& config, const AuthoredScene* scene,
                      const GpuRegionPrograms* regions, std::string_view preparation_fault,
                      const vulkan::Buffer* live_edit_controls,
                      const vulkan::Buffer* cached_edit_controls,
                      render::TemporalUpscaler* upscaler,
                      VkExtent2D dlss_render_extent) {
    live_edit_controls_ = live_edit_controls;
    cached_edit_controls_ = cached_edit_controls;
    field_ = field;
    source_tracing_ = config.surface_trace != "sampled";
    shadow_candidate_ = source_shadow_pipeline_enabled(config);
    prepared_candidate_ = source_prepared_cones_enabled(config);
    if (cached_edit_controls_ &&
        (live_edit_controls_ || !field || !scene || regions || config.surface_trace != "sampled" ||
         config.csg_evaluation != "full" || config.acceleration_variant != "off" ||
         config.cached_quality != "ordinary" || config.lighting != "atmosphere" || config.debug ||
         config.ao || config.clouds || config.ground || config.occluder ||
         !config.query_workload_output.empty()))
        throw std::invalid_argument(
            "private cached edit renderer requires Cached opaque atmosphere and a frozen field");
    if (live_edit_controls_ &&
        (!field || !regions || !scene || config.lighting != "atmosphere" ||
         config.surface_trace != "analytic" || config.acceleration_variant != "root-far" ||
         config.debug || config.ao || config.clouds || config.ground || config.occluder ||
         config.cached_quality != "ordinary" || config.query_stepping != "ordinary" ||
         prepared_candidate_))
        throw std::invalid_argument(
            "private live renderer requires a frozen field/regions and opaque analytic atmosphere");
    acceleration_uploads_ = config.acceleration_variant != "off" ? &gpu : nullptr;
    display_extent_ = extent;
    upscaler_ = upscaler;
    if (config.upscaler == "spatial-quality") {
        scene_extent_ = render::quality_spatial_render_extent(extent);
        effective_upscaler_ = "spatial-quality";
        upscaler_reason_ = "linear HDR spatial reconstruction; no temporal history";
    } else if (config.upscaler == "dlss-quality") {
        if (!upscaler_ || !dlss_render_extent.width || !dlss_render_extent.height)
            throw std::invalid_argument("DLSS Quality target was not created for the SDF view");
        scene_extent_ = dlss_render_extent;
        effective_upscaler_ = "dlss-quality";
        upscaler_reason_ = "active";
        upscaler_motion_ = std::make_unique<render::TemporalCameraMotion>(
            device, slots, std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "motion.comp.spv");
    } else {
        scene_extent_ = {extent.width * config.supersample, extent.height * config.supersample};
        effective_upscaler_ = "native";
        upscaler_reason_ = config.supersample == 1 ? "native resolution" : "native supersampling";
    }
    upscaler_history_age_ = 0;
    upscaler_reset_reason_ = render::TemporalResetReason::FirstFrame;
    executor_.resize(slots);
    lighting_.emplace(device, slots);
    const auto host_buffer = [&](std::size_t bytes, bool local = false) {
        return std::make_unique<vulkan::Buffer>(
            device, vulkan::BufferConfig{.size = bytes,
                                         .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                         .memory_properties =
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                             (local ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0U)});
    };
    if (!acceleration_uploads_)
        scene_program_ = host_buffer(16 + authored_max_instructions * sizeof(SceneInstruction),
                                     config.comparison_residency == "program" ||
                                         config.comparison_residency == "all");
    if (config.comparison_stage != "off" && !acceleration_uploads_) {
        const auto memory = [&](const char* name, const vulkan::Buffer& buffer, bool local) {
            VkMemoryRequirements requirements;
            vkGetBufferMemoryRequirements(device.handle(), buffer.handle(), &requirements);
            const auto type = device.find_memory_type(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                    (local ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0U));
            VkPhysicalDeviceMemoryProperties properties;
            vkGetPhysicalDeviceMemoryProperties(device.physical_device(), &properties);
            std::cout << "sdf: study memory " << name << " type=" << type
                      << " heap=" << properties.memoryTypes[type].heapIndex
                      << " flags=" << properties.memoryTypes[type].propertyFlags << '\n';
        };
        memory("program", *scene_program_,
               config.comparison_residency == "program" || config.comparison_residency == "all");
        if (regions) {
            const bool local =
                config.comparison_residency == "regions" || config.comparison_residency == "all";
            memory("region-cells", *regions->cells, local);
            memory("region-refs", *regions->references, local);
        }
    }
    scene_materials_ = host_buffer(16 * sizeof(SurfaceMaterial));
    scene_patterns_ = host_buffer(16 * sizeof(MaterialPattern));
    scene_domains_ = host_buffer(16 * sizeof(math::Mat4));
    scene_libraries_ = host_buffer(16 * sizeof(procedural::SurfaceLibrary));
    const bool study = config.lighting != "atmosphere";
    if (study) {
        const auto start = std::chrono::steady_clock::now();
        if (!config.material_environment_path.empty()) {
            if (config.lighting != "material-study")
                throw std::invalid_argument("diagnostic HDR requires material-study lighting");
            const auto image = asset::load_hdr_image(config.material_environment_path);
            study_environment_.emplace(render::create_pbr_environment_from_equirectangular(
                device, gpu,
                {.width = image.width, .height = image.height, .rgba32f = image.rgba32f}));
        } else
            study_environment_.emplace(render::create_generated_pbr_environment(device, gpu));
        std::cout << "sdf: study IBL setup_ms="
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               start)
                         .count()
                  << '\n';
        scene_optics_ = host_buffer(16 * sizeof(MaterialOptics));
    }
    create_environment(device, gpu, config, slots);
    update_authored(device, scene, regions);
    if (acceleration_uploads_) {
        const auto memory = [&](const char* name, const vulkan::Buffer& buffer) {
            VkMemoryRequirements requirements;
            vkGetBufferMemoryRequirements(device.handle(), buffer.handle(), &requirements);
            const auto type = device.find_memory_type(requirements.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkPhysicalDeviceMemoryProperties properties;
            vkGetPhysicalDeviceMemoryProperties(device.physical_device(), &properties);
            std::cout << "sdf: acceleration memory " << name << " type=" << type
                      << " heap=" << properties.memoryTypes[type].heapIndex
                      << " flags=" << properties.memoryTypes[type].propertyFlags << '\n';
        };
        memory("program", *scene_program_);
        memory("region-cells", *regions->cells);
        memory("region-refs", *regions->references);
        std::cout << "sdf: acceleration root low=" << regions->root.low.x << ','
                  << regions->root.low.y << ',' << regions->root.low.z
                  << " high=" << regions->root.high.x << ',' << regions->root.high.y << ','
                  << regions->root.high.z << " empty=" << regions->root.empty
                  << " far_cells=" << regions->far_cells << '\n';
    }
    for (std::uint32_t i = 0; i < slots; ++i)
        preview_buffers_.push_back(host_buffer(authored_max_leaves * sizeof(SceneInstruction)));
    std::vector<vulkan::DescriptorSetBindingConfig> bindings{
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT}};
    if (field) {
        bindings.push_back({0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT});
        bindings.push_back(
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT});
    }
    if (regions) {
        bindings.push_back({16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT});
        bindings.push_back({17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT});
    }
    if (live_edit_controls_ || cached_edit_controls_)
        bindings.push_back({21, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT});
    if (prepared_candidate_) {
        bindings.push_back({19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT});
    }
    quality_capture_ = !config.quality_output.empty();
    quality_snapshot_saved_ = false;
    if (!config.query_workload_output.empty() || quality_capture_) {
        const std::uint64_t count =
            quality_capture_
                ? static_cast<std::uint64_t>(scene_extent_.width) * scene_extent_.height
                : ((scene_extent_.width + 7U) / 8U) *
                      static_cast<std::uint64_t>((scene_extent_.height + 3U) / 4U);
        const auto bytes = 16U + count * (quality_capture_ ? 8U : 256U);
        if (bytes + static_cast<std::uint64_t>(scene_extent_.width) * scene_extent_.height * 8U >
            32U * 1024U * 1024U)
            throw std::invalid_argument("query workload exceeds 32 MiB storage cap");
        query_workload_ = host_buffer(bytes);
        const std::array<std::uint32_t, 4> dimensions{scene_extent_.width, scene_extent_.height,
                                                      quality_capture_ ? 1U : 8U,
                                                      static_cast<std::uint32_t>(count)};
        query_workload_->upload(dimensions.data(), sizeof(dimensions));
        query_workload_path_ =
            quality_capture_ ? config.quality_output : config.query_workload_output;
        for (auto& binding : bindings)
            binding.stage_flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back({18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT});
        bindings.push_back(
            {20, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT});
    }
    if (study) {
        bindings.push_back({7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT});
        for (std::uint32_t binding = 8; binding <= 12; ++binding)
            bindings.push_back(
                {binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT});
    }
    if (prepared_candidate_)
        cone_descriptor_bindings_ = bindings;
    descriptors_ = std::make_unique<vulkan::DescriptorSetArray>(
        device, vulkan::DescriptorSetInfo(bindings, slots));
    if (preparation_fault == "mode-descriptors")
        throw CapacityExceeded("injected rendering-mode descriptor preparation failure");
    if (field)
        update_field(device, *field);
    for (std::uint32_t i = 0; i < slots; ++i) {
        vulkan::DescriptorWriteBatch writes;
        writes.uniform_buffer(descriptors_->set(i), 2, lighting_->buffer({i, slots}).handle(),
                              sizeof(LightingUniforms));
        writes.storage_buffer(descriptors_->set(i), 3, scene_program_->handle(),
                              scene_program_->size());
        if (live_edit_controls_)
            writes.storage_buffer(descriptors_->set(i), 21, live_edit_controls_->handle(),
                                  live_edit_controls_->size());
        if (cached_edit_controls_)
            writes.storage_buffer(descriptors_->set(i), 21, cached_edit_controls_->handle(),
                                  cached_edit_controls_->size());
        writes.storage_buffer(descriptors_->set(i), 4, scene_materials_->handle(),
                              scene_materials_->size());
        writes.storage_buffer(descriptors_->set(i), 5, preview_buffers_[i]->handle(),
                              preview_buffers_[i]->size());
        writes.storage_buffer(descriptors_->set(i), 6, scene_patterns_->handle(),
                              scene_patterns_->size());
        writes.storage_buffer(descriptors_->set(i), 13, scene_domains_->handle(),
                              scene_domains_->size());
        writes.storage_buffer(descriptors_->set(i), 14, scene_libraries_->handle(),
                              scene_libraries_->size());
        if (prepared_candidate_)
            writes.storage_buffer(descriptors_->set(i), 19, scene_cones_->handle(),
                                  scene_cones_->size());
        if (query_workload_)
            writes.storage_buffer(descriptors_->set(i), 18, query_workload_->handle(),
                                  query_workload_->size());
        if (regions) {
            writes.storage_buffer(descriptors_->set(i), 16, regions->cells->handle(),
                                  regions->cells->size());
            writes.storage_buffer(descriptors_->set(i), 17, regions->references->handle(),
                                  regions->references->size());
        }
        if (study_environment_) {
            writes.storage_buffer(descriptors_->set(i), 7, scene_optics_->handle(),
                                  scene_optics_->size());
            const auto textures = render::pbr_environment_texture_bindings(*study_environment_);
            writes.combined_image_sampler(descriptors_->set(i), 8, textures.irradiance_sampler,
                                          textures.irradiance_view, textures.irradiance_layout);
            writes.combined_image_sampler(descriptors_->set(i), 9, textures.prefiltered_sampler,
                                          textures.prefiltered_view, textures.prefiltered_layout);
            writes.combined_image_sampler(descriptors_->set(i), 10, textures.brdf_lut_sampler,
                                          textures.brdf_lut_view, textures.brdf_lut_layout);
        }
        writes.update(device);
        if (study_environment_)
            bind_environment(device, {i, slots});
    }
    const auto path = std::filesystem::path(CUBEY_SDF_SHADER_DIR);
    std::string fragment = config.surface_trace == "analytic"
                               ? (study ? "material_analytic.frag.spv" : "sdf_analytic.frag.spv")
                           : config.surface_trace == "hybrid"
                               ? (study ? "material_hybrid.frag.spv" : "sdf_hybrid.frag.spv")
                           : study ? "material_study.frag.spv"
                           : scene ? "sdf_authored.frag.spv"
                                   : "sdf.frag.spv";
    if (regions && source_tracing_)
        fragment.insert(fragment.find(".frag"), "_pruned");
    if (config.cached_quality != "ordinary")
        fragment.insert(0, cached_quality_prefix(config));
    if (config.comparison_stage != "off") {
        if (study)
            throw std::invalid_argument("comparison stages require atmosphere lighting");
        fragment.insert(0, "investigate_");
    }
    if (config.comparison_tight_root)
        fragment.insert(0, "tight_");
    if (config.acceleration_variant != "off")
        fragment.insert(0, config.acceleration_variant == "control" ? "accel_"
                           : config.acceleration_variant == "root-far"
                               ? "root_far_"
                               : config.acceleration_variant + "_");
    if (config.frame_profile == "fixed")
        fragment = "profile_fixed_" + config.comparison_stage + ".frag.spv";
    else if (config.frame_profile != "off" && config.frame_profile != "runtime")
        fragment = "profile_" + config.frame_profile + ".frag.spv";
    if (live_edit_controls_)
        fragment = "live_edit_sdf.frag.spv";
    if (cached_edit_controls_)
#ifdef CUBEY_SDF_STROKE_STUDY
        fragment = "cached_stroke_sdf.frag.spv";
#else
        fragment = "cached_edit_sdf.frag.spv";
#endif
    const std::array stages{
        render::vertex_shader_file(path / "sdf.vert.spv"),
        render::fragment_shader_file(path / (quality_capture_ ? "quality_" + fragment : fragment))};
    const std::array layouts{descriptors_->layout()};
    render::MaterialPassInfo pass{
        .label = "sdf.trace",
        .push_constants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Camera)}},
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL};
    const auto pipeline_start = std::chrono::steady_clock::now();
    pipeline_.emplace(
        device, render::GraphicsPipelineFileResourceConfig{.extent = scene_extent_,
                                                           .color_format = hdr_format,
                                                           .depth_format = VK_FORMAT_D32_SFLOAT,
                                                           .shader_stage_files = stages,
                                                           .descriptor_set_layouts = layouts,
                                                           .material_pass = pass});
    shader_pipeline_count_ = 1;
    if (preparation_fault == "mode-pipeline")
        throw CapacityExceeded("injected rendering-mode pipeline preparation failure");
    if (source_shader_variants_enabled(config)) {
        const std::array lit_files{render::vertex_shader_file(path / "sdf.vert.spv"),
                                   render::fragment_shader_file(path / ("lit_" + fragment))};
        const VkSpecializationMapEntry entry{0, 0, sizeof(VkBool32)};
        const auto create_lit = [&](auto& destination, VkBool32 single_material) {
            auto files = lit_files;
            if (single_material && shadow_candidate_)
                files[1] = render::fragment_shader_file(path / ("shadow_lit_" + fragment));
            if (single_material && prepared_candidate_)
                files[1] = render::fragment_shader_file(
                    path / ((shadow_candidate_ ? "combined_lit_" : "prepared_lit_") + fragment));
            if (query_workload_)
                files[1] = render::fragment_shader_file(
                    path / (single_material
                                ? (prepared_candidate_ ? "query_validate_combined.frag.spv"
                                                       : "query_validate_shadow.frag.spv")
                                : "query_validate_reference.frag.spv"));
            if (single_material && config.query_stepping != "ordinary") {
                const std::string prefix(query_step_prefix(config));
                const std::string selected =
                    query_workload_
                        ? (prepared_candidate_ ? "query_validate_combined.frag.spv"
                                               : "query_validate_shadow.frag.spv")
                        : (prepared_candidate_ ? "combined_lit_" : "shadow_lit_") + fragment;
                files[1] = render::fragment_shader_file(path / (prefix + selected));
            }
            if (quality_capture_) {
                std::string selected =
                    single_material
                        ? (prepared_candidate_ ? "combined_lit_" : "shadow_lit_") + fragment
                        : "lit_" + fragment;
                if (single_material)
                    selected.insert(0, query_step_prefix(config));
                files[1] = render::fragment_shader_file(path / ("quality_" + selected));
            }
            const render::ShaderProgram program(device, files);
            std::vector<VkPipelineShaderStageCreateInfo> lit_stages(program.stages().begin(),
                                                                    program.stages().end());
            const VkSpecializationInfo specialization{1, &entry, sizeof(single_material),
                                                      &single_material};
            for (auto& stage : lit_stages)
                if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
                    stage.pSpecializationInfo = &specialization;
            destination.emplace(
                device, render::GraphicsPipelineResourceConfig{.extent = scene_extent_,
                                                               .color_format = hdr_format,
                                                               .depth_format = VK_FORMAT_D32_SFLOAT,
                                                               .shader_stages = lit_stages,
                                                               .descriptor_set_layouts = layouts,
                                                               .material_pass = pass});
            ++shader_pipeline_count_;
        };
        create_lit(lit_general_pipeline_, VK_FALSE);
        create_lit(lit_single_pipeline_, VK_TRUE);
    }
    if (query_workload_) {
        const bool single = shader_route(config) == SourceShaderRoute::LitSingleMaterial;
        const std::string variant = quality_capture_ && !source_tracing_ ? "sampled"
                                    : single ? (prepared_candidate_ ? "combined" : "shadow")
                                             : "reference";
        const std::string prefix = !source_tracing_ ? std::string(cached_quality_prefix(config))
                                   : single         ? std::string(query_step_prefix(config))
                                                    : "";
        const std::array<VkPushConstantRange, 1> push{
            {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Camera)}}};
        query_workload_pipeline_.emplace(
            device,
            render::ComputePipelineResourceConfig{
                .shader_stage = render::compute_shader_file(
                    path / (cached_edit_controls_
                                ?
#ifdef CUBEY_SDF_STROKE_STUDY
                                "cached_stroke_quality_workload.comp.spv"
#else
                                "cached_edit_quality_workload.comp.spv"
#endif
                                : live_edit_controls_ ? "live_edit_quality_workload.comp.spv"
                                                      : prefix +
                                                            (quality_capture_ ? "quality_workload_"
                                                                              : "query_workload_") +
                                                            variant + ".comp.spv")),
                .descriptor_set_layouts = layouts,
                .push_constants = push});
        const std::array uv_stages{render::vertex_shader_file(path / "sdf.vert.spv"),
                                   render::fragment_shader_file(path / "query_camera.frag.spv")};
        query_camera_pipeline_.emplace(device, render::GraphicsPipelineFileResourceConfig{
                                                   .extent = scene_extent_,
                                                   .color_format = VK_FORMAT_R32G32_SFLOAT,
                                                   .shader_stage_files = uv_stages,
                                                   .material_pass = {.label = "sdf.query-camera"}});
        query_camera_sampler_.emplace(
            device, vulkan::SamplerConfig{.min_filter = VK_FILTER_NEAREST,
                                          .mag_filter = VK_FILTER_NEAREST,
                                          .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE});
    }
    shader_pipeline_setup_ms_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pipeline_start)
            .count();
    std::cout << "sdf: shader pipelines=" << shader_pipeline_count_
              << " setup_ms=" << shader_pipeline_setup_ms_
              << " route=" << source_shader_route_name(shader_route(config)) << '\n';
    std::cout << "sdf: source optimization requested=" << config.source_optimization
              << " effective=" << source_optimization(config) << '\n';
    const std::array raster_stages{render::vertex_shader_file(path / "occluder.vert.spv"),
                                   render::fragment_shader_file(path / "occluder.frag.spv")};
    pass.push_constants = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera)}};
    occluder_pipeline_.emplace(
        device, render::GraphicsPipelineFileResourceConfig{.extent = scene_extent_,
                                                           .color_format = hdr_format,
                                                           .depth_format = VK_FORMAT_D32_SFLOAT,
                                                           .shader_stage_files = raster_stages,
                                                           .material_pass = pass});
    post_.create_materials(device, {.frame_slot_count = slots});
    const std::array post_stages{render::vertex_shader_file(path / "forward_pbr_post.vert.spv"),
                                 render::fragment_shader_file(path / "forward_pbr_post.frag.spv")};
    post_.create_pipeline(
        device, {.extent = extent, .color_format = format, .shader_stage_files = post_stages});
    if (!config.common.profile_output_prefix.empty() || config.common.print_frame_stats)
        profiler_.emplace(device, slots, 16);
    if (profiler_)
        whole_profiler_.emplace(device, slots, 1);
    recorded_frames_.resize(slots);
}
void Renderer::update_field(vulkan::Device& device, const Field& field) {
    ++temporal_content_generation_;
    field_ = &field;
    for (std::uint32_t i = 0; i < descriptors_->size(); ++i) {
        vulkan::DescriptorWriteBatch writes;
        writes.storage_buffer(descriptors_->set(i), 0, field.nodes().handle(),
                              field.nodes().size());
        writes.combined_image_sampler(descriptors_->set(i), 1, field.atlas().sampler().handle(),
                                      field.atlas().view(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        writes.update(device);
    }
}
void Renderer::save_quality_snapshot(const std::filesystem::path& path) {
    if (!quality_capture_ || !query_workload_)
        throw std::logic_error("quality snapshot has no capture buffer");
    std::vector<std::uint8_t> bytes(query_workload_->size());
    query_workload_->download(bytes.data(), bytes.size());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw std::runtime_error("cannot write quality snapshot");
    quality_snapshot_saved_ = true;
}
void Renderer::destroy() {
    if (upscaler_motion_)
        upscaler_view_state_.invalidate(render::TemporalResetReason::TargetChanged);
    temporal_.destroy();
    temporal_frame_.reset();
    upscaler_frame_.reset();
    upscaler_motion_.reset();
    upscaler_ = nullptr;
    temporal_used_ = false;
    if (query_workload_ && !quality_snapshot_saved_) {
        std::vector<std::uint8_t> bytes(query_workload_->size());
        query_workload_->download(bytes.data(), bytes.size());
        std::ofstream file(query_workload_path_, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!file)
            throw std::runtime_error("cannot write query workload payload");
    }
    query_workload_pipeline_.reset();
    query_camera_pipeline_.reset();
    query_camera_sampler_.reset();
    query_workload_.reset();
    query_workload_path_.clear();
    // Hosts drain frame submissions before destroying resources. Preserve the
    // final slots too; collecting only when a slot is reused loses tail samples.
    if (profile_)
        for (std::uint32_t i = 0; i < recorded_frames_.size(); ++i) {
            if (!recorded_frames_[i])
                continue;
            for (auto* timer : {profiler_ ? &*profiler_ : nullptr,
                                whole_profiler_ ? &*whole_profiler_ : nullptr}) {
                if (!timer)
                    continue;
                timer->collect(i);
                for (const auto& timing : timer->latest_timings())
                    profile_->record_gpu_span(*recorded_frames_[i], timing.label,
                                              timing.milliseconds);
            }
        }
    profile_ = nullptr;
    if (material_atmosphere_)
        std::cout << "sdf: environment finish sky_generation="
                  << atmosphere_.reflection_probe().snapshot().generation << " cloud_generation="
                  << (clouds_enabled_ ? atmosphere_.clouds().snapshot().generation : 0) << '\n';
    executor_.clear();
    profiler_.reset();
    whole_profiler_.reset();
    recorded_frames_.clear();
    post_.destroy();
    occluder_pipeline_.reset();
    lit_single_pipeline_.reset();
    lit_general_pipeline_.reset();
    pipeline_.reset();
    shader_pipeline_count_ = 0;
    published_material_count_ = 0;
    shader_pipeline_setup_ms_ = 0;
    quality_pipeline_descriptors_.reset();
    descriptors_.reset();
    scene_optics_.reset();
    study_environment_.reset();
    scene_materials_.reset();
    scene_patterns_.reset();
    scene_domains_.reset();
    scene_libraries_.reset();
    scene_program_.reset();
    live_edit_controls_ = nullptr;
    cached_edit_controls_ = nullptr;
    scene_cones_.reset();
    cone_data_.reset();
    cone_descriptor_bindings_.clear();
    cone_builds_ = cone_allocation_bytes_ = cone_peak_bytes_ = 0;
    cone_preparations_ = cone_staging_payload_peak_ = 0;
    cone_preparation_ms_ = 0;
    preview_buffers_.clear();
    lighting_.reset();
    sky_background_.destroy();
    atmosphere_.destroy();
    atmosphere_textures_.reset();
    material_atmosphere_ = false;
    clouds_enabled_ = false;
    cloud_sky_only_ = false;
    cloud_sky_generation_ = 0;
    field_ = nullptr;
    regions_ = nullptr;
    scene_extent_ = {};
    display_extent_ = {};
}
std::unique_ptr<vulkan::Buffer> Renderer::prepare_program(const AuthoredScene* scene) {
    if (!acceleration_uploads_)
        return {};
    auto bytes = authored_program_bytes(scene);
    if (scene) {
        const float guard = authored_evaluation_guard(*scene);
        std::memcpy(bytes.data() + 12, &guard, 4);
    }
    return std::make_unique<vulkan::Buffer>(vulkan::upload_device_buffer(
        *acceleration_uploads_, bytes.data(), bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
}
Renderer::PreparedAuthored Renderer::prepare_authored(vulkan::Device& device,
                                                      const AuthoredScene* scene,
                                                      const GpuRegionPrograms* regions,
                                                      std::string_view fault) {
    PreparedAuthored next;
    const auto start = std::chrono::steady_clock::now();
    next.program = prepare_program(scene);
    if (!prepared_candidate_)
        return next;
    if (!scene || !regions || scene_optics_)
        throw std::invalid_argument(
            "prepared cone publication requires authored analytic atmosphere regions");
    if (std::any_of(scene->optics.begin(), scene->optics.end(),
                    [](const auto& o) { return !default_optics(o); }))
        throw std::invalid_argument("non-default material optics require material-study lighting");
    if (fault == "cone-prepare")
        throw CapacityExceeded("injected cone preparation failure");
    if (!cone_data_ || !cone_data_->matches(*scene)) {
        next.cone_data.emplace(prepare_cones(*scene));
        ++cone_preparations_;
        const auto bytes =
            std::max<std::size_t>(16, next.cone_data->entries.size() * sizeof(math::Vec4));
        const math::Vec4 empty{0};
        next.cones = std::make_unique<vulkan::Buffer>(vulkan::upload_device_buffer(
            *acceleration_uploads_,
            next.cone_data->entries.empty() ? &empty : next.cone_data->entries.data(), bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device.handle(), next.cones->handle(), &requirements);
        next.cone_allocation_bytes = requirements.size;
        next.cone_peak_bytes = cone_allocation_bytes_ + requirements.size;
        // Diagnostics include discarded candidates, not only activated ones.
        cone_peak_bytes_ = std::max(cone_peak_bytes_, next.cone_peak_bytes);
        cone_staging_payload_peak_ = std::max<std::uint64_t>(cone_staging_payload_peak_, bytes);
    }
    if (fault == "cone-upload")
        throw CapacityExceeded("injected failure after cone upload");
    const auto host = [&](const void* data, std::size_t bytes) {
        auto buffer = std::make_unique<vulkan::Buffer>(
            device,
            vulkan::BufferConfig{.size = bytes,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
        buffer->upload(data, bytes);
        return buffer;
    };
    const SurfaceMaterial material;
    const MaterialPattern pattern;
    next.materials =
        host(scene->materials.empty() ? &material : scene->materials.data(),
             std::max<std::size_t>(1, scene->materials.size()) * sizeof(SurfaceMaterial));
    next.patterns =
        host(scene->patterns.empty() ? &pattern : scene->patterns.data(),
             std::max<std::size_t>(1, scene->patterns.size()) * sizeof(MaterialPattern));
    std::array<math::Mat4, 16> domains;
    domains.fill(math::Mat4{1});
    if (!scene->material_domains.empty())
        std::copy(scene->material_domains.begin(), scene->material_domains.end(), domains.begin());
    next.domains = host(domains.data(), sizeof(domains));
    std::array<procedural::SurfaceLibrary, 16> libraries;
    if (!scene->libraries.empty())
        std::copy(scene->libraries.begin(), scene->libraries.end(), libraries.begin());
    next.libraries = host(libraries.data(), sizeof(libraries));
    if (fault == "appearance-upload")
        throw CapacityExceeded("injected failure after appearance uploads");
    if (descriptors_) {
        // A complete unpublished descriptor generation keeps update/allocations
        // out of activation. Compatible layouts preserve the three pipelines.
        next.descriptors = std::make_unique<vulkan::DescriptorSetArray>(
            device, vulkan::DescriptorSetInfo(cone_descriptor_bindings_, descriptors_->size()));
        const auto* cones = next.cones ? next.cones.get() : scene_cones_.get();
        vulkan::DescriptorWriteBatch writes;
        for (std::uint32_t i = 0; i < next.descriptors->size(); ++i) {
            writes.uniform_buffer(next.descriptors->set(i), 2,
                                  lighting_->buffer({i, next.descriptors->size()}).handle(),
                                  sizeof(LightingUniforms));
            for (const auto& [binding, buffer] :
                 std::array<std::pair<unsigned, const vulkan::Buffer*>, 9>{
                     {{3, next.program.get()},
                      {4, next.materials.get()},
                      {5, preview_buffers_[i].get()},
                      {6, next.patterns.get()},
                      {13, next.domains.get()},
                      {14, next.libraries.get()},
                      {16, regions->cells.get()},
                      {17, regions->references.get()},
                      {19, cones}}}) {
                writes.storage_buffer(next.descriptors->set(i), binding, buffer->handle(),
                                      buffer->size());
            }
            if (query_workload_)
                writes.storage_buffer(next.descriptors->set(i), 18, query_workload_->handle(),
                                      query_workload_->size());
        }
        writes.update(device);
    }
    if (fault == "descriptors")
        throw CapacityExceeded("injected failure after candidate descriptors");
    next.preparation_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return next;
}
void Renderer::update_authored(vulkan::Device& device, const AuthoredScene* scene,
                               const GpuRegionPrograms* regions) {
    update_authored(device, scene, regions, PreparedAuthored{});
}
void Renderer::update_authored(vulkan::Device& device, const AuthoredScene* scene,
                               const GpuRegionPrograms* regions, PreparedAuthored prepared) {
    ++temporal_content_generation_;
    temporal_transmission_ =
        scene && std::any_of(scene->optics.begin(), scene->optics.end(),
                             [](const auto& optics) { return optics.settings.y > 0; });
    if (prepared_candidate_) {
        if (!prepared.materials)
            prepared = prepare_authored(device, scene, regions);
        // Candidate buffers and descriptor writes are already complete. The app
        // retires all prior in-flight use before this non-allocating activation.
        scene_program_ = std::move(prepared.program);
        scene_materials_ = std::move(prepared.materials);
        scene_patterns_ = std::move(prepared.patterns);
        scene_domains_ = std::move(prepared.domains);
        scene_libraries_ = std::move(prepared.libraries);
        if (prepared.cones) {
            scene_cones_ = std::move(prepared.cones);
            cone_data_ = std::move(prepared.cone_data);
            cone_allocation_bytes_ = prepared.cone_allocation_bytes;
            cone_peak_bytes_ = std::max(cone_peak_bytes_, prepared.cone_peak_bytes);
            ++cone_builds_;
        }
        if (prepared.descriptors) {
            // The private compute probe borrows the initial set layout. Keep
            // that layout alive across compatible publication generations.
            if (quality_capture_ && !quality_pipeline_descriptors_)
                quality_pipeline_descriptors_ = std::move(descriptors_);
            descriptors_ = std::move(prepared.descriptors);
        }
        cone_preparation_ms_ = prepared.preparation_ms;
        regions_ = regions;
        published_material_count_ = scene->materials.size();
        return;
    }
    if (!scene_optics_ && scene &&
        std::any_of(scene->optics.begin(), scene->optics.end(),
                    [](const auto& o) { return !default_optics(o); }))
        throw std::invalid_argument("non-default material optics require material-study lighting");
    auto bytes = authored_program_bytes(scene);
    if (scene && source_tracing_) {
        const float guard = authored_evaluation_guard(*scene);
        std::memcpy(bytes.data() + 12, &guard, 4);
    }
    if (acceleration_uploads_)
        scene_program_ = prepared.program ? std::move(prepared.program) : prepare_program(scene);
    else
        scene_program_->upload(bytes.data(), bytes.size());
    if (scene_optics_) {
        const MaterialOptics fallback_optics;
        scene_optics_->upload(scene ? scene->optics.data() : &fallback_optics,
                              scene ? scene->optics.size() * sizeof(MaterialOptics)
                                    : sizeof(MaterialOptics));
    }
    std::array<math::Mat4, 16> domains;
    domains.fill(math::Mat4{1});
    if (scene && !scene->material_domains.empty())
        std::copy(scene->material_domains.begin(), scene->material_domains.end(), domains.begin());
    scene_domains_->upload(domains.data(), sizeof(domains));
    std::array<procedural::SurfaceLibrary, 16> libraries;
    if (scene && !scene->libraries.empty())
        std::copy(scene->libraries.begin(), scene->libraries.end(), libraries.begin());
    scene_libraries_->upload(libraries.data(), sizeof(libraries));
    const SurfaceMaterial fallback;
    const MaterialPattern plain;
    scene_materials_->upload(scene ? scene->materials.data() : &fallback,
                             scene ? scene->materials.size() * sizeof(SurfaceMaterial)
                                   : sizeof(SurfaceMaterial));
    scene_patterns_->upload(scene ? scene->patterns.data() : &plain,
                            scene ? scene->patterns.size() * sizeof(MaterialPattern)
                                  : sizeof(MaterialPattern));
    regions_ = regions;
    if (descriptors_ && regions) {
        vulkan::DescriptorWriteBatch writes;
        for (std::uint32_t i = 0; i < descriptors_->size(); ++i) {
            writes.storage_buffer(descriptors_->set(i), 3, scene_program_->handle(),
                                  scene_program_->size());
            writes.storage_buffer(descriptors_->set(i), 16, regions->cells->handle(),
                                  regions->cells->size());
            writes.storage_buffer(descriptors_->set(i), 17, regions->references->handle(),
                                  regions->references->size());
        }
        writes.update(device);
    }
    published_material_count_ = scene ? scene->materials.size() : 0;
}
SourceShaderRoute Renderer::shader_route(const SdfConfig& config) const {
    if (!lit_general_pipeline_ || !lit_single_pipeline_)
        return SourceShaderRoute::General;
    return select_source_shader(config, published_material_count_);
}
void Renderer::record(vulkan::Device& device, VkCommandBuffer command,
                      render::ColorTargetView target, render::FrameSlot slot, bool present,
                      const SdfConfig& config, float yaw, float pitch, float distance,
                      profiling::ProfileRecorder* profile, std::uint64_t frame_index,
                      const BoxEdit* cutter, float cutter_opacity,
                      std::span<const SceneInstruction> previews, float frame_time_ms) {
    profile_ = profile;
    pending_profile_slot_ = slot.index;
    const auto route = shader_route(config);
    const auto* selected_pipeline =
        route == SourceShaderRoute::LitSingleMaterial ? &*lit_single_pipeline_
        : route == SourceShaderRoute::LitGeneral      ? &*lit_general_pipeline_
                                                      : &*pipeline_;
    const bool dlss_effective = config.upscaler == "dlss-quality";
    if (profile) {
        const auto mode =
            std::find(render_modes.begin(), render_modes.end(), config.effective_render_mode);
        profile->record_metric(frame_index, "sdf.render_mode", "effective",
                               static_cast<double>(mode - render_modes.begin()));
        profile->record_metric(frame_index, "sdf.render_mode", "named",
                               config.named_render_mode ? 1 : 0);
        profile->record_metric(frame_index, "sdf.render_mode", "supersample", config.supersample);
        const auto upscaler_code = [](std::string_view name) {
            return name == "spatial-quality" ? 1U : name == "dlss-quality" ? 2U : 0U;
        };
        profile->record_metric(frame_index, "sdf.upscaler", "requested",
                               upscaler_code(config.upscaler));
        profile->record_metric(frame_index, "sdf.upscaler", "effective",
                               upscaler_code(effective_upscaler_));
        profile->record_metric(frame_index, "sdf.upscaler", "render_width", scene_extent_.width);
        profile->record_metric(frame_index, "sdf.upscaler", "render_height", scene_extent_.height);
        profile->record_metric(frame_index, "sdf.upscaler", "display_width", display_extent_.width);
        profile->record_metric(frame_index, "sdf.upscaler", "display_height",
                               display_extent_.height);
        profile->record_metric(frame_index, "sdf.upscaler", "spatial_no_history",
                               config.upscaler == "spatial-quality" ? 1 : 0);
        profile->record_metric(frame_index, "sdf.cached_quality", "mode",
                               config.cached_quality == "ordinary"         ? 0
                               : config.cached_quality == "refine-0.05"    ? 5
                               : config.cached_quality == "refine-0.03125" ? 32
                               : config.cached_quality == "source-normal"  ? 6
                               : config.cached_quality == "refine-normal"  ? 38
                                                                           : 60);
        profile->record_metric(frame_index, "sdf.shader_selection", "route",
                               static_cast<unsigned>(route));
        profile->record_metric(
            frame_index, "sdf.shader_selection", "guarded_stepping",
            route != SourceShaderRoute::LitSingleMaterial || config.query_stepping == "ordinary" ? 0
            : config.query_stepping == "relaxed-1.2" ? 12
            : config.query_stepping == "relaxed-1.5" ? 15
                                                     : 50);
        profile->record_metric(frame_index, "sdf.shader_selection", "published_material_count",
                               static_cast<double>(published_material_count_));
        profile->record_metric(frame_index, "sdf.shader_selection", "pipeline_count",
                               shader_pipeline_count_);
        profile->record_metric(frame_index, "sdf.shader_selection", "pipeline_setup_ms",
                               shader_pipeline_setup_ms_);
        profile->record_metric(
            frame_index, "sdf.shader_selection", "shadow_optimized",
            shadow_candidate_ && route == SourceShaderRoute::LitSingleMaterial ? 1 : 0);
        profile->record_metric(
            frame_index, "sdf.shader_selection", "cones_optimized",
            prepared_candidate_ && route == SourceShaderRoute::LitSingleMaterial ? 1 : 0);
        profile->record_metric(frame_index, "sdf.prepared_cones", "builds",
                               static_cast<double>(cone_builds_));
        profile->record_metric(frame_index, "sdf.prepared_cones", "payload_bytes",
                               static_cast<double>(scene_cones_ ? scene_cones_->size() : 0));
        profile->record_metric(frame_index, "sdf.prepared_cones", "allocation_bytes",
                               static_cast<double>(cone_allocation_bytes_));
        profile->record_metric(frame_index, "sdf.prepared_cones", "device_generations_peak_bytes",
                               static_cast<double>(cone_peak_bytes_));
        profile->record_metric(frame_index, "sdf.prepared_cones", "preparations",
                               static_cast<double>(cone_preparations_));
        profile->record_metric(frame_index, "sdf.prepared_cones", "staging_payload_peak_bytes",
                               static_cast<double>(cone_staging_payload_peak_));
        profile->record_metric(frame_index, "sdf.prepared_cones", "publication_prepare_ms",
                               cone_preparation_ms_);
        profile->record_metric(frame_index, "sdf.resources", "field_bytes",
                               static_cast<double>(field_ ? field_->stats().allocation_bytes : 0));
        profile->record_metric(frame_index, "sdf.resources", "region_bytes",
                               static_cast<double>(regions_ ? regions_->allocation_bytes : 0));
        profile->record_metric(frame_index, "sdf.resources", "region_references",
                               static_cast<double>(regions_ ? regions_->reference_count : 0));
        profile->record_metric(frame_index, "sdf.resources", "region_far_cells",
                               regions_ ? regions_->far_cells : 0);
        profile->record_metric(frame_index, "sdf.resources", "region_fallback_cells",
                               regions_ ? regions_->fallback_cells : 0);
        profile->record_metric(frame_index, "sdf.resources", "region_cpu_scratch_peak_bytes",
                               static_cast<double>(regions_ ? regions_->scratch_peak_bytes : 0));
        profile->record_metric(frame_index, "sdf.resources", "region_build_upload_ms",
                               regions_ ? regions_->build_upload_ms : 0);
    }
    if (profiler_) {
        if (recorded_frames_[slot.index]) {
            profiler_->collect(slot.index);
            if (profile)
                for (const auto& timing : profiler_->latest_timings())
                    profile->record_gpu_span(*recorded_frames_[slot.index], timing.label,
                                             timing.milliseconds);
        }
        profiler_->begin_frame(command, slot.index);
    }
    if (whole_profiler_) {
        if (recorded_frames_[slot.index]) {
            whole_profiler_->collect(slot.index);
            if (profile)
                for (const auto& timing : whole_profiler_->latest_timings())
                    profile->record_gpu_span(*recorded_frames_[slot.index], timing.label,
                                             timing.milliseconds);
        }
        whole_profiler_->begin_frame(command, slot.index);
    }
    if (profiler_)
        recorded_frames_[slot.index] = frame_index;
    const vulkan::GpuTimestampScope whole(whole_profiler_ ? &*whole_profiler_ : nullptr, command,
                                          slot.index, "sdf.whole_scene");
    const math::Vec3 target_point{0, config.target_y, 0};
    const math::Vec3 eye =
        target_point + math::Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                  std::cos(pitch) * std::cos(yaw)} *
                           distance;
    prepare_environment(device, command, slot, config, eye, target_point);
    const auto projection = math::perspective(glm::radians(45.0F),
                                              static_cast<float>(target.extent.width) /
                                                  static_cast<float>(target.extent.height),
                                              0.01F, 100.0F);
    const auto view = glm::lookAt(eye, target_point, math::Vec3{0, 1, 0});
    auto vp = projection * view;
    const bool raw_diagnostic = (config.debug > 0 && config.debug < 10) || config.debug == 12;
    const bool taa_requested =
        config.upscaler == "native" && host::native_taa_requested(config.common);
    const int bypass = raw_diagnostic || config.debug != 0            ? 1
                       : config.supersample != 1                      ? 2
                       : clouds_enabled_                              ? 3
                       : temporal_transmission_                       ? 4
                       : query_workload_pipeline_ || quality_capture_ ? 5
                       : live_edit_controls_ || cached_edit_controls_ || !previews.empty() ||
                               (cutter && cutter_opacity > 0)
                           ? 6
                       : config.lighting_cycle != "frozen" ? 7
                       : taa_requested && !temporal_.created() &&
                             !render::temporal_aa_unsupported_reason(device).empty()
                           ? 8
                           : 0;
    const bool taa_effective = taa_requested && bypass == 0;
    constexpr std::array reasons{"Active",
                                 "Bypassed for diagnostics",
                                 "Requires supersampling 1x",
                                 "Cloud motion unsupported",
                                 "Transmission unsupported",
                                 "Bypassed for raw query output",
                                 "Live edit/cutter preview unsupported",
                                 "Dynamic lighting unsupported",
                                 "Unsupported device"};
    temporal_status_ = taa_requested ? reasons.at(static_cast<std::size_t>(bypass)) : "Off";
    if (config.upscaler != "native")
        temporal_status_ = "Suppressed by selected upscaler";
    if (taa_effective || dlss_effective) {
        // Camera motion and post-only exposure are excluded. Static appearance
        // changes advance the same accepted-frame identity for TAA and DLSS.
        std::uint64_t settings = 1469598103934665603ULL;
        auto mix = [&](auto value) {
            const auto bytes = std::as_bytes(std::span(&value, 1));
            for (auto byte : bytes)
                settings = (settings ^ std::to_integer<unsigned>(byte)) * 1099511628211ULL;
        };
        mix(config.sun_elevation);
        mix(config.sun_azimuth);
        mix(config.roughness);
        mix(config.shadow_softness);
        mix(config.shadows);
        mix(config.ao);
        mix(config.ground);
        mix(config.clay);
        mix(config.procedural);
        mix(config.bump);
        mix(config.material_fast_path);
        mix(config.debug);
        if (temporal_settings_ != settings) {
            ++temporal_content_generation_;
            temporal_settings_ = settings;
        }
    }
    if (taa_effective) {
        if (!temporal_.created())
            temporal_.create(
                device,
                {.extent = scene_extent_,
                 .frame_slot_count = slot.count,
                 .motion_shader = std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "motion.comp.spv",
                 .resolve_shader =
                     std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "resolve.comp.spv"});
        temporal_frame_ =
            temporal_.begin_frame(slot, {.projection = projection,
                                         .view = view,
                                         .content_generation = temporal_content_generation_,
                                         .reset = !temporal_used_});
        vp = temporal_frame_->jittered_view_projection;
    }
    if (dlss_effective) {
        if (!upscaler_ || !upscaler_motion_)
            throw std::logic_error("DLSS Quality was requested without a live target");
        upscaler_frame_ =
            upscaler_view_state_.prepare({.projection = projection,
                                          .view = view,
                                          .content_generation = temporal_content_generation_},
                                         scene_extent_);
        vp = upscaler_frame_->jittered_view_projection;
        upscaler_history_age_ = upscaler_frame_->history_age;
        upscaler_reset_reason_ = upscaler_frame_->reset_reason;
        upscaler_reason_ = "active";
    }
    temporal_used_ = taa_effective;
    if (profile) {
        if (dlss_effective) {
            profile->record_metric(frame_index, "sdf.upscaler", "history_age",
                                   upscaler_frame_->history_age);
            profile->record_metric(frame_index, "sdf.upscaler", "reset_reason",
                                   static_cast<unsigned>(upscaler_frame_->reset_reason));
            profile->record_metric(frame_index, "sdf.upscaler", "jitter_x",
                                   upscaler_frame_->sample_offset_pixels.x);
            profile->record_metric(frame_index, "sdf.upscaler", "jitter_y",
                                   upscaler_frame_->sample_offset_pixels.y);
        } else {
            profile->record_metric(frame_index, "sdf.upscaler", "history_age", 0);
            profile->record_metric(frame_index, "sdf.upscaler", "reset_reason",
                                   static_cast<unsigned>(render::TemporalResetReason::None));
        }
        profile->record_metric(frame_index, "aa", "requested", taa_requested ? 1 : 0);
        profile->record_metric(frame_index, "aa", "effective", taa_effective ? 1 : 0);
        profile->record_metric(frame_index, "aa", "bypass_reason", taa_requested ? bypass : 0);
        if (taa_effective) {
            const auto memory = temporal_.metrics();
            profile->record_metric(frame_index, "taa", "history_age", temporal_frame_->history_age);
            profile->record_metric(frame_index, "taa", "reset_reason",
                                   static_cast<unsigned>(temporal_frame_->reset_reason));
            profile->record_metric(frame_index, "taa", "jitter_x",
                                   temporal_frame_->sample_offset_pixels.x);
            profile->record_metric(frame_index, "taa", "jitter_y",
                                   temporal_frame_->sample_offset_pixels.y);
            profile->record_metric(frame_index, "taa", "persistent_requested_bytes",
                                   static_cast<double>(memory.persistent_requested_bytes));
            profile->record_metric(frame_index, "taa", "per_slot_transient_requested_bytes",
                                   static_cast<double>(memory.per_slot_transient_requested_bytes));
            profile->record_metric(frame_index, "taa", "uniform_requested_bytes",
                                   static_cast<double>(memory.uniform_requested_bytes));
            profile->record_metric(frame_index, "taa", "persistent_image_allocation_bytes",
                                   static_cast<double>(memory.persistent_image_allocation_bytes));
            profile->record_metric(frame_index, "taa", "per_slot_transient_image_allocation_bytes",
                                   static_cast<double>(memory.per_slot_transient_image_allocation_bytes));
            profile->record_metric(frame_index, "taa", "uniform_allocation_bytes",
                                   static_cast<double>(memory.uniform_allocation_bytes));
        }
    }
    if (material_atmosphere_) {
        const auto forward = glm::normalize(target_point - eye),
                   right = glm::normalize(glm::cross(forward, math::Vec3{0, 1, 0}));
        sky_background_.upload(
            slot,
            atmosphere_
                .frame(
                    {.view_rays =
                         {.right_aspect = {right, static_cast<float>(display_extent_.width) /
                                                      static_cast<float>(display_extent_.height)},
                          .up_tan_half_fovy = {glm::cross(right, forward),
                                               std::tan(glm::radians(22.5F))},
                          .forward =
                              {forward +
                                   (taa_effective
                                        ? (right *
                                               (2.0F * temporal_frame_->sample_offset_pixels.x /
                                                static_cast<float>(scene_extent_.height)) -
                                           glm::cross(right, forward) *
                                               (2.0F * temporal_frame_->sample_offset_pixels.y /
                                                static_cast<float>(scene_extent_.height))) *
                                              std::tan(glm::radians(22.5F))
                                        : math::Vec3{0}),
                               0}}})
                .background);
    }
    const auto& light = atmosphere_.lighting();
    LightingUniforms uniforms{};
    uniforms.atmosphere = atmosphere_.frame({}).background;
    uniforms.direct_color = {light.primary_light_color * light.primary_light_intensity, 0};
    for (unsigned i = 0; i < 9; ++i)
        uniforms.irradiance[i] = {light.diffuse_irradiance_sh[i], 0};
    uniforms.material =
        config.clay ? math::Vec4{0.42F, 0.42F, 0.42F, 1} : math::Vec4{0.55F, 0.27F, 0.095F, 0};
    uniforms.quality = {config.roughness, config.shadow_softness, config.ao ? 1.0F : 0.0F, -0.48F};
    if (study_environment_ && !material_atmosphere_)
        uniforms.direct_color = {1.8F, 1.6F, 1.3F, 0};
    uniforms.surface_options = {config.procedural ? 1.0F : 0.0F, config.bump ? 1.0F : 0.0F,
                                config.material_fast_path ? 1.0F : 0.0F,
                                config.lighting == "material-furnace" ? 1.0F : 0.0F};
    const auto environment_bindings =
        study_environment_
            ? (material_atmosphere_ ? atmosphere_.pbr_environment_bindings(*study_environment_)
                                    : render::pbr_environment_texture_bindings(*study_environment_))
            : render::PbrEnvironmentTextureBindings{};
    uniforms.transport_options = {
        static_cast<float>(config.glass_crossings), environment_bindings.prefiltered_blend,
        static_cast<float>(environment_bindings.prefiltered_mip_levels
                               ? environment_bindings.prefiltered_mip_levels - 1
                               : 4),
        material_atmosphere_ ? 1.0F : 0.0F};
    if (config.lighting == "material-furnace")
        uniforms.direct_color = math::Vec4{0};
    if (cutter && config.cutter_cue && config.debug == 0) {
        uniforms.cutter = gpu_edit(*cutter);
        uniforms.cutter_style = {cutter_opacity, 0.0025F, 0, 0};
    }
    if (!previews.empty() && config.cutter_cue && config.debug == 0) {
        if (previews.size() > authored_max_leaves)
            throw std::invalid_argument("authored preview exceeds primitive capacity");
        preview_buffers_.at(slot.index)->upload(previews.data(), previews.size_bytes());
        uniforms.preview_style.x = cutter_opacity;
        uniforms.preview_style.y = static_cast<float>(previews.size());
    }
    lighting_->upload(slot, uniforms);
    const Camera camera{
        glm::inverse(vp),
        {eye, static_cast<float>(field_ ? field_->atlas_axis : 0)},
        {study_environment_ && !material_atmosphere_ ? glm::normalize(math::Vec3{-.35F, .42F, .84F})
                                                     : light.primary_light_direction,
         0},
        {static_cast<float>(config.debug), static_cast<float>(config.shadows),
         config.ground ? 1.0F : 0.0F,
         config.comparison_stage == "primary"                                             ? 1.F
         : config.comparison_stage == "normal"                                            ? 2.F
         : config.comparison_stage == "material"                                          ? 3.F
         : config.comparison_stage == "shadow"                                            ? 4.F
         : cached_source_normals(config) && config.cached_quality != "source-normal-zero" ? 1.F
                                                                                          : 0.F},
        {glm::normalize(target_point - eye),
         2.0F * std::tan(glm::radians(22.5F)) / static_cast<float>(scene_extent_.height)}};
    post_.upload(slot, render::hdr_post_uniforms(
                           target.format, raw_diagnostic ? 0.0F : config.exposure,
                           raw_diagnostic ? render::PbrTonemap::Linear : render::PbrTonemap::Aces));
    render::RenderGraphBuilder builder;
    const auto output = builder.import_color_target(
        "sdf.output", target,
        present ? render::render_graph_acquired_color_texture_state()
                : render::render_graph_color_attachment_texture_state(),
        present ? render::render_graph_present_texture_state()
                : render::render_graph_color_attachment_texture_state());
    const auto scene = builder.create_texture(
        render::hdr_scene_color_texture_desc("sdf.hdr", scene_extent_, hdr_format));
    const auto depth =
        builder.create_texture({.label = "sdf.depth",
                                .extent = {scene_extent_.width, scene_extent_.height, 1},
                                .format = VK_FORMAT_D32_SFLOAT,
                                .aspects = VK_IMAGE_ASPECT_DEPTH_BIT});
    std::optional<render::RenderGraphTextureHandle> atlas;
    std::optional<render::RenderGraphBufferHandle> nodes;
    if (field_) {
        atlas = builder.import_texture(
            {.label = "sdf.atlas",
             .extent = field_->atlas().extent(),
             .format = VK_FORMAT_R16_SFLOAT,
             .aspects = VK_IMAGE_ASPECT_COLOR_BIT},
            field_->atlas().handle(), field_->atlas().view(),
            render::RenderGraphTextureState{
                VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
            render::RenderGraphTextureState{VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT});
        nodes = builder.import_buffer(
            {"sdf.nodes", field_->nodes().size()}, field_->nodes().handle(),
            render::RenderGraphBufferState{VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT});
    }
    const auto lighting = builder.import_buffer({"sdf.lighting", sizeof(LightingUniforms)},
                                                lighting_->buffer(slot).handle());
    const auto program =
        builder.import_buffer({"sdf.scene", scene_program_->size()}, scene_program_->handle());
    const auto materials = builder.import_buffer({"sdf.materials", scene_materials_->size()},
                                                 scene_materials_->handle());
    const auto domains =
        builder.import_buffer({"sdf.domains", scene_domains_->size()}, scene_domains_->handle());
    const auto libraries = builder.import_buffer({"sdf.libraries", scene_libraries_->size()},
                                                 scene_libraries_->handle());
    const auto patterns =
        builder.import_buffer({"sdf.patterns", scene_patterns_->size()}, scene_patterns_->handle());
    const auto previews_buffer =
        builder.import_buffer({"sdf.previews", preview_buffers_.at(slot.index)->size()},
                              preview_buffers_.at(slot.index)->handle());
    std::optional<render::RenderGraphBufferHandle> query_buffer, region_cells, region_refs;
    std::optional<render::RenderGraphBufferHandle> live_controls;
    if (live_edit_controls_)
        live_controls = builder.import_buffer({"sdf.live-controls", live_edit_controls_->size()},
                                              live_edit_controls_->handle());
    if (cached_edit_controls_)
        live_controls =
            builder.import_buffer({"sdf.cached-edit-controls", cached_edit_controls_->size()},
                                  cached_edit_controls_->handle());
    if (regions_) {
        region_cells = builder.import_buffer({"sdf.regions", regions_->cells->size()},
                                             regions_->cells->handle());
        region_refs = builder.import_buffer({"sdf.region_refs", regions_->references->size()},
                                            regions_->references->handle());
    }
    if (query_workload_pipeline_) {
        const auto ray_uv =
            builder.create_texture({.label = "sdf.query-camera",
                                    .extent = {scene_extent_.width, scene_extent_.height, 1},
                                    .format = VK_FORMAT_R32G32_SFLOAT,
                                    .aspects = VK_IMAGE_ASPECT_COLOR_BIT});
        query_buffer = builder.import_buffer(
            {"sdf.query-workload", query_workload_->size()}, query_workload_->handle(),
            render::RenderGraphBufferState{VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT},
            render::RenderGraphBufferState{VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT});
        builder.add_pass("sdf.query-camera", render::RenderGraphQueueDomain::Graphics)
            .write_color(ray_uv)
            .execute([&, ray_uv](const render::RenderGraphExecutionContext& ctx) {
                render::record_render_target_pass(
                    ctx.recorder(),
                    render::render_target_view(render::resolved_color_target_view(ctx, ray_uv)), {},
                    [&](const vulkan::CommandRecorder& pass) {
                        pass.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           query_camera_pipeline_->pipeline());
                        render::record_fullscreen_triangle(pass);
                    });
            });
        auto replay =
            builder.add_pass("sdf.query-workload", render::RenderGraphQueueDomain::Compute);
        replay.read_texture(ray_uv, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .read_write_storage_buffer(*query_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .read_uniform_buffer(lighting, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        for (auto resource : {program, materials, patterns, domains, libraries})
            replay.read_storage_buffer(resource, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (live_controls)
            replay.read_storage_buffer(*live_controls, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (region_cells)
            replay.read_storage_buffer(*region_cells, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (region_refs)
            replay.read_storage_buffer(*region_refs, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (atlas)
            replay.read_texture(*atlas, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (nodes)
            replay.read_storage_buffer(*nodes, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        replay.execute([&, ray_uv, camera](const render::RenderGraphExecutionContext& ctx) {
            const auto sampled = render::resolved_sampled_texture_view(ctx, ray_uv);
            vulkan::DescriptorWriteBatch writes;
            writes.combined_image_sampler(descriptors_->set(slot.index), 20,
                                          query_camera_sampler_->handle(), sampled.view,
                                          sampled.layout);
            writes.update(device);
            const auto& query = ctx.recorder();
            query.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                                query_workload_pipeline_->pipeline());
            query.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                      query_workload_pipeline_->layout(), 0,
                                      descriptors_->set(slot.index));
            query.push_constants(query_workload_pipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 camera);
            const auto count = quality_capture_ ? scene_extent_.width * scene_extent_.height
                                                : ((scene_extent_.width + 7U) / 8U) *
                                                      ((scene_extent_.height + 3U) / 4U);
            query.dispatch((count + 63U) / 64U, 1, 1);
        });
    }
    auto trace_pass = builder.add_pass("sdf.trace", render::RenderGraphQueueDomain::Graphics);
    if (live_controls)
        trace_pass.read_storage_buffer(*live_controls, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (query_buffer)
        trace_pass.read_storage_buffer(*query_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (atlas)
        trace_pass.read_texture(*atlas, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (nodes)
        trace_pass.read_storage_buffer(*nodes, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (regions_) {
        trace_pass.read_storage_buffer(*region_cells, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        trace_pass.read_storage_buffer(*region_refs, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    trace_pass.read_uniform_buffer(lighting, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(program, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(materials, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(patterns, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(domains, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(libraries, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(previews_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .write_color(scene)
        .write_depth(depth)
        .execute([&, camera, scene, depth,
                  selected_pipeline](const render::RenderGraphExecutionContext& ctx) {
            render::record_render_target_pass(
                ctx.recorder(),
                render::render_target_view(render::resolved_color_target_view(ctx, scene),
                                           render::resolved_depth_target_view(ctx, depth)),
                {.color = render::color_clear_value(0, 0, 0, 1),
                 .depth = VkClearValue{.depthStencil = {1, 0}}},
                [&](const vulkan::CommandRecorder& pass) {
                    if (material_atmosphere_ && config.debug == 0)
                        render::record_fullscreen_pipeline_draw(
                            pass, {.pipeline = &sky_background_.pipeline(),
                                   .descriptor_set = sky_background_.material().set(slot)});
                    pass.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       selected_pipeline->pipeline());
                    pass.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             selected_pipeline->layout(), 0,
                                             descriptors_->set(slot.index));
                    pass.push_constants(selected_pipeline->layout(), VK_SHADER_STAGE_FRAGMENT_BIT,
                                        0, camera);
                    render::record_fullscreen_triangle(pass);
                    if (config.occluder) {
                        pass.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           occluder_pipeline_->pipeline());
                        pass.push_constants(occluder_pipeline_->layout(),
                                            VK_SHADER_STAGE_VERTEX_BIT, 0, camera);
                        pass.draw(6);
                    }
                });
        });
    std::optional<render::CloudLayerRuntimeFrame> cloud_product;
    std::optional<render::RenderGraphTextureHandle> dlss_motion;
    auto post_scene = scene;
    if (taa_effective) {
        const auto motion = temporal_.declare_camera_motion(builder, depth, *temporal_frame_);
        post_scene = temporal_.declare_resolve(builder, scene, motion, *temporal_frame_);
    } else if (dlss_effective) {
        dlss_motion = upscaler_motion_->declare(builder, slot, depth, *upscaler_frame_);
        post_scene = upscaler_->declare(builder, {.color = scene,
                                                  .depth = depth,
                                                  .motion = *dlss_motion,
                                                  .view = *upscaler_frame_,
                                                  .output_extent = display_extent_,
                                                  .frame_time_ms = frame_time_ms});
    }
    if (clouds_enabled_ && config.cloud_backdrop && config.debug == 0) {
        auto& clouds = atmosphere_.clouds();
        const auto frame = cloud_frame(eye, target_point);
        cloud_product = clouds.declare_surface_product(builder, slot, frame);
        post_scene = builder.create_texture(
            render::hdr_scene_color_texture_desc("sdf.clouded", scene_extent_, hdr_format));
        clouds.declare_surface_composite(builder, post_scene, *cloud_product, slot, scene, depth);
    }
    builder.add_pass("sdf.post", render::RenderGraphQueueDomain::Graphics)
        .read_texture(post_scene)
        .write_color(output)
        .execute([&, output](const render::RenderGraphExecutionContext& ctx) {
            post_.record_pass(ctx.recorder(), render::resolved_color_target_view(ctx, output),
                              slot);
        });
    const auto graph = builder.compile();
    executor_.record(
        {.device = &device,
         .command_buffer = command,
         // Pose probes drain the GPU after every frame. Reuse one
         // graph-resource slot so the UV image is not duplicated.
         .frame_slot = quality_capture_ ? render::FrameSlot{0, slot.count} : slot,
         .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording,
         .profiler = profiler_ ? &*profiler_ : nullptr},
        graph, [&](const render::RenderGraphResourceSet& resources) {
            if (taa_effective)
                temporal_.update_descriptors(device, graph, resources, *temporal_frame_);
            if (dlss_motion)
                upscaler_motion_->update_descriptors(device, slot, graph, resources, depth,
                                                     *dlss_motion);
            post_.update_scene_color_descriptor(device, slot, graph, resources, post_scene);
            if (cloud_product)
                atmosphere_.clouds().update_surface_descriptors(device, slot, graph, resources,
                                                                *cloud_product, scene, depth);
        });
    if (cloud_product)
        atmosphere_.clouds().complete_surface_frame(slot, *cloud_product);
}
void Renderer::frame_submitted(vulkan::GpuSubmissionTicket ticket) {
    pending_profile_slot_.reset();
    if (temporal_frame_) {
        temporal_.commit_frame(temporal_frame_->token, ticket);
        temporal_frame_.reset();
    }
    if (upscaler_frame_) {
        upscaler_view_state_.commit(upscaler_frame_->token, ticket);
        upscaler_frame_.reset();
    }
}
void Renderer::frame_discarded() {
    if (pending_profile_slot_ && *pending_profile_slot_ < recorded_frames_.size())
        recorded_frames_[*pending_profile_slot_].reset();
    pending_profile_slot_.reset();
    temporal_.discard_frame();
    temporal_frame_.reset();
    if (upscaler_frame_) {
        upscaler_view_state_.discard();
        upscaler_frame_.reset();
    }
}
} // namespace cubey::projects::sdf
