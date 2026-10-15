#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_site_water.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/engine/terrain_surface_context_resources.h>
#include <cubey/engine/terrain_surface_patch_scene.h>
#include <cubey/engine/terrain_water_capture_runtime.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/terrain_surface_patch_whitewater.h>
#include <cubey/terrain/terrain_surface_field.h>
#include <cubey/vulkan/buffer.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cubey {
namespace {
asset::TerrainWaterCapture inspection_surface(const asset::TerrainSite& site,
                                              const asset::TerrainRasterHeightSource& source) {
    asset::TerrainWaterCapture surface;
    const float span = site.level_spans_m[0];
    const auto intervals = std::uint32_t(std::ceil(span / 32.0F));
    if (intervals == 0 || intervals > 1024)
        throw std::runtime_error("terrain site inspection grid budget exceeded");
    surface.grid = {intervals + 1U, intervals + 1U, span / float(intervals),
                    site.center_xz_m - math::Vec2{span * .5F}};
    const auto count = std::size_t(surface.grid.width) * surface.grid.height;
    surface.source_elevation_sha256 = source.provenance().elevation_sha256;
    surface.depth_m.assign(count, 0);
    surface.velocity_xz_m_per_s.assign(count, math::Vec2{0});
    surface.bed_m.reserve(count);
    for (std::uint32_t z = 0; z < surface.grid.height; ++z)
        for (std::uint32_t x = 0; x < surface.grid.width; ++x)
            surface.bed_m.push_back(source.sample_height(
                {.world_xz = surface.grid.sample_origin_xz_m +
                             math::Vec2{float(x), float(z)} * surface.grid.cell_size_m}));
    return surface;
}

void validate_extension(const asset::TerrainWaterCapture& capture,
                        const asset::TerrainRasterHeightSource& anchor,
                        const asset::TerrainRasterHeightSource& extension) {
    const auto read = [](const auto& path) {
        std::ifstream stream(path);
        return nlohmann::json::parse(stream);
    };
    const auto a = read(anchor.provenance().manifest_path);
    const auto b = read(extension.provenance().manifest_path);
    const auto& ag = a.at("grid");
    const auto& bg = b.at("grid");
    const auto& ai = a.at("provenance").at("model_native_origin");
    const auto& bi = b.at("provenance").at("model_native_origin");
    const double spacing = ag.at("sample_spacing_m").get<double>();
    if (a.at("seed") != b.at("seed") || a.at("source") != b.at("source") ||
        a.at("height") != b.at("height") || ag.at("axis_mapping") != bg.at("axis_mapping") ||
        bg.at("sample_spacing_m").get<double>() != spacing ||
        std::abs(bg.at("sample_origin_x_m").get<double>() -
                 ag.at("sample_origin_x_m").get<double>() -
                 (bi.at("j").get<double>() - ai.at("j").get<double>()) * spacing) > .001 ||
        std::abs(bg.at("sample_origin_z_m").get<double>() -
                 ag.at("sample_origin_z_m").get<double>() -
                 (bi.at("i").get<double>() - ai.at("i").get<double>()) * spacing) > .001)
        throw std::runtime_error("terrain context extension is not the same world/coordinates");
    // Preserve the original capture hash and rounded-bed check. Verify the
    // stronger native source identity separately: sub-cell interpolation using
    // a different float raster origin need not reproduce the same rounding.
    asset::validate_terrain_water_capture_source(capture, anchor);
    const auto origin = anchor.bounds().minimum_xz;
    for (std::uint32_t z = 0; z < anchor.height(); ++z)
        for (std::uint32_t x = 0; x < anchor.width(); ++x) {
            const auto p = origin + math::Vec2{float(x), float(z)} * anchor.sample_spacing_m();
            if (std::abs(double(extension.sample_height({.world_xz = p})) -
                         double(anchor.sample_height({.world_xz = p}))) > .0001)
                throw std::runtime_error("terrain context extension changes native anchor terrain");
        }
}
} // namespace
struct TerrainWaterCaptureRuntime::State {
    asset::TerrainRasterHeightSource source;
    asset::TerrainWaterCapture capture;
    bool terrain_only = false;
    bool externally_lit = false;
    std::filesystem::path shaders;
    TerrainSurfacePatchScene scene;
    render::RenderGraphFrameExecutor graph;
    AtmosphereEnvironmentRuntime daylight;
    std::unique_ptr<TerrainSurfaceEnvironment> weather;
    bool weather_enabled = false;
    vulkan::GpuRuntime* gpu = nullptr;
    std::optional<render::AtmosphereBackgroundAtlasResources> atlases;
    std::optional<vulkan::Buffer> bed, depth, velocity, zeros, coverage;
    std::optional<terrain::TerrainSurfaceContextProduct> context;
    std::unique_ptr<TerrainSurfaceContextResources> context_resources;
    std::optional<terrain::TerrainSurfacePatchLod> fixed_mesh;
    std::optional<vulkan::Buffer> mesh_coordinates, mesh_indices;
    std::optional<vulkan::DescriptorSetBundle> mesh_fields;
    std::optional<vulkan::DescriptorSetBundle> fields;
    std::optional<vulkan::GpuTimestampProfiler> whole_scene_profiler;
    std::vector<math::Vec4> velocity_values, weights;
    render::TerrainSurfacePatchWhitewaterSelection whitewater;
    render::TerrainSurfacePatchMaterial material = render::terrain_surface_patch_macro_material();
    State(const std::filesystem::path& capture_path, const std::filesystem::path& heightfield,
          std::filesystem::path directory)
        : source(heightfield), capture(asset::load_terrain_water_capture(capture_path)),
          shaders(std::move(directory)), scene(shaders) {
        asset::validate_terrain_water_capture_source(capture, source);
        initialize_surface();
    }
    State(const asset::TerrainSite& site, std::filesystem::path directory)
        : source(site.heightfield), capture(inspection_surface(site, source)),
          shaders(std::move(directory)), scene(shaders) {
        terrain_only = true;
        initialize_surface();
    }
    State(const asset::TerrainSite& site, const std::filesystem::path& capture_path,
          std::filesystem::path directory)
        : source(site.heightfield), capture(asset::load_terrain_water_capture(capture_path)),
          shaders(std::move(directory)), scene(shaders) {
        asset::validate_terrain_site_water_capture(capture, site);
        asset::validate_terrain_water_capture_source(capture, source);
        initialize_surface();
    }
    void initialize_surface() {
        const auto center = capture.grid.center_xz_m();
        material.terrain_world_x_m = center.x;
        material.terrain_world_z_m = center.y;
        std::optional<asset::TerrainRasterClimateSource> climate;
        const auto companion =
            source.provenance().manifest_path.parent_path() / "surface-fields.json";
        if (std::filesystem::exists(companion)) {
            climate.emplace(companion);
            asset::validate_terrain_climate_binding(source, *climate);
        }
        const auto field = terrain::make_terrain_surface_field(source, source.bounds(),
                                                               climate ? &*climate : nullptr);
        const auto& g = capture.grid;
        weights.reserve(capture.bed_m.size());
        velocity_values.reserve(capture.bed_m.size());
        for (std::uint32_t z = 0; z < g.height; ++z)
            for (std::uint32_t x = 0; x < g.width; ++x) {
                weights.push_back(field.sample(g.sample_origin_xz_m +
                                               math::Vec2{float(x), float(z)} * g.cell_size_m));
                const auto v = capture.velocity_xz_m_per_s[std::size_t(z) * g.width + x];
                velocity_values.push_back(
                    {v.x, v.y, capture.depth_m[std::size_t(z) * g.width + x] > 0.0F ? 1.0F : 0.0F,
                     0.0F});
            }
        whitewater = render::terrain_surface_patch_whitewater_seeds(
            g.width, g.height, g.cell_size_m, std::span<const float>{capture.bed_m},
            std::span<const float>{capture.depth_m},
            std::span<const math::Vec2>{capture.velocity_xz_m_per_s},
            std::uint32_t(material.water_whitewater_budget));
    }
    render::TerrainSurfacePatchFields field_view() const {
        return {*bed, *depth, *velocity, *coverage, fields->set()};
    }
    render::TerrainSurfacePatchGrid grid() const {
        return {capture.grid.width, capture.grid.height, capture.grid.cell_size_m, 0.0001F};
    }
};
TerrainWaterCaptureRuntime::TerrainWaterCaptureRuntime(const std::filesystem::path& capture,
                                                       const std::filesystem::path& heightfield,
                                                       std::filesystem::path shaders) {
    const auto started = std::chrono::steady_clock::now();
    state_ = std::make_unique<State>(capture, heightfield, std::move(shaders));
    std::printf(
        "terrain_water_capture: bound rain-fed state %.6f s, %.3f mm/h, %ux%u / %.3f m; "
        "load_and_appearance_ms=%.3f; hydraulic_dispatches=0\n",
        state_->capture.capture_time_s, state_->capture.rain_mm_per_hour,
        state_->capture.grid.width, state_->capture.grid.height, state_->capture.grid.cell_size_m,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count());
}
TerrainWaterCaptureRuntime::~TerrainWaterCaptureRuntime() = default;
TerrainWaterCaptureRuntime::TerrainWaterCaptureRuntime(const asset::TerrainSite& site,
                                                       const std::filesystem::path& capture,
                                                       std::filesystem::path shaders) {
    state_ = std::make_unique<State>(site, capture, std::move(shaders));
    std::printf("terrain_site_water: %s world=%.12s; captured %.3f h, %ux%u / %.0f m; "
                "hydraulic_dispatches=0\n",
                site.id.c_str(), site.world_id.c_str(), state_->capture.capture_time_s / 3600.0,
                state_->capture.grid.width, state_->capture.grid.height,
                state_->capture.grid.cell_size_m);
}
TerrainWaterCaptureRuntime::TerrainWaterCaptureRuntime(const asset::TerrainSite& site,
                                                       std::filesystem::path shaders) {
    state_ = std::make_unique<State>(site, std::move(shaders));
    std::printf("terrain_site: %s world=%.12s; terrain-only inspection, no hydraulic recording; "
                "hydraulic_dispatches=0\n",
                site.id.c_str(), site.world_id.c_str());
}
void TerrainWaterCaptureRuntime::ensure_resources(const vulkan::Device& device,
                                                  vulkan::GpuRuntime& gpu, std::uint32_t slots,
                                                  render::ColorTargetView target,
                                                  bool external_environment) {
    auto& s = *state_;
    if (s.graph.frame_slot_count() != slots)
        s.graph.resize(slots);
    if (!s.fields) {
        const auto started = std::chrono::steady_clock::now();
        const auto upload = [&](const void* data, VkDeviceSize bytes) {
            return vulkan::upload_device_buffer(gpu, data, bytes,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        };
        const auto bytes = s.capture.bed_m.size() * sizeof(float);
        s.bed.emplace(upload(s.capture.bed_m.data(), bytes));
        s.depth.emplace(upload(s.capture.depth_m.data(), bytes));
        s.velocity.emplace(
            upload(s.velocity_values.data(), s.velocity_values.size() * sizeof(math::Vec4)));
        const std::vector<float> zero(s.capture.bed_m.size(), 0.0F);
        s.zeros.emplace(upload(zero.data(), bytes));
        const std::array<float, 5> coverage{1.0F, 1.0F, 0.0F, 0.0F, 1.0F};
        s.coverage.emplace(upload(coverage.data(), sizeof(coverage)));
        std::vector<vulkan::DescriptorSetBindingConfig> bindings;
        for (std::uint32_t i = 0; i <= 8U; ++i)
            bindings.push_back(
                {.binding = i,
                 .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT});
        s.fields.emplace(device, vulkan::DescriptorSetInfo(bindings));
        render::MaterialDescriptorWriter writer(s.fields->set());
        for (std::uint32_t i = 0; i <= 8U; ++i) {
            const auto& b = i == 0U   ? *s.bed
                            : i == 1U ? *s.depth
                            : i == 2U ? *s.velocity
                            : i == 8U ? *s.coverage
                                      : *s.zeros;
            writer.storage_buffer(i, b.handle(), b.size());
        }
        writer.update(device);
        s.whole_scene_profiler.emplace(device, slots, 1U);
        std::printf(
            "terrain_water_capture_upload: once; retained_input_gpu_bytes=%llu wall_ms=%.3f\n",
            static_cast<unsigned long long>(retained_gpu_field_bytes()),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count());
    }
    if (s.fixed_mesh && !s.mesh_fields) {
        s.mesh_coordinates.emplace(
            vulkan::upload_device_buffer(gpu, s.fixed_mesh->coordinates.data(),
                                         s.fixed_mesh->coordinates.size() * sizeof(math::Vec2),
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        s.mesh_indices.emplace(vulkan::upload_device_buffer(
            gpu, s.fixed_mesh->indices.data(), s.fixed_mesh->indices.size() * sizeof(std::uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
        const std::array bindings{
            vulkan::DescriptorSetBindingConfig{.binding = 0,
                                               .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                               .stage_flags = VK_SHADER_STAGE_VERTEX_BIT}};
        s.mesh_fields.emplace(device, vulkan::DescriptorSetInfo(bindings));
        render::MaterialDescriptorWriter(s.mesh_fields->set())
            .storage_buffer(0, s.mesh_coordinates->handle(), s.mesh_coordinates->size())
            .update(device);
        std::printf(
            "terrain_fixed_lod_upload: once; bytes=%llu vertices=%zu triangles=%zu\n",
            static_cast<unsigned long long>(s.mesh_coordinates->size() + s.mesh_indices->size()),
            s.fixed_mesh->coordinates.size(), s.fixed_mesh->indices.size() / 3);
    }
    if (s.context && !s.context_resources)
        s.context_resources =
            std::make_unique<TerrainSurfaceContextResources>(device, gpu, *s.context);
    const render::TerrainSurfacePatchSurface surface{
        s.capture.grid.width, s.capture.grid.height, {}, {}, s.weights};
    s.scene.ensure_resources(device, gpu, slots, target, s.grid(), s.capture.bed_m, s.field_view(),
                             true, &surface, true);
    s.gpu = &gpu;
    if (external_environment)
        return;
    if (s.weather && s.weather_enabled)
        s.weather->ensure_resources(device, gpu, slots, target, s.shaders);
    else if (!s.daylight.resources_created()) {
        s.atlases.emplace(render::create_atmosphere_background_placeholder_textures(device, gpu));
        s.daylight.create_resources(device, {.reflection_extent = 64U,
                                             .reflection_mip_levels = 5U,
                                             .frame_slot_count = slots,
                                             .atmosphere_textures = s.atlases->bindings()});
        s.daylight.create_pipelines(
            device, {.atmosphere_vertex_shader = s.shaders / "atmosphere.vert.spv",
                     .atmosphere_fragment_shader = s.shaders / "atmosphere.frag.spv",
                     .reflection_prefilter_vertex_shader = s.shaders / "atmosphere.vert.spv",
                     .reflection_prefilter_fragment_shader =
                         s.shaders / "atmosphere_reflection_prefilter.frag.spv"});
        s.daylight.set_environment(render::terrain_surface_patch_fixed_daylight());
    }
}
void TerrainWaterCaptureRuntime::record(
    const vulkan::Device& device, VkCommandBuffer commands, render::FrameSlot slot,
    render::ColorTargetView target, bool present, const render::TerrainSurfacePatchCamera& camera,
    double seconds, bool water, bool profile, bool context_visible, unsigned ground_view,
    bool fixed_lod, const render::TerrainSurfacePatchEnvironment* external_environment,
    const render::TerrainSurfacePatchOpaqueLayer* foreground) {
    auto& s = *state_;
    auto* profiler = profile ? &*s.whole_scene_profiler : nullptr;
    if (profiler)
        profiler->begin_frame(commands, slot.index);
    vulkan::GpuTimestampScope whole_scene(profiler, commands, slot.index,
                                          "terrain captured patch whole scene");
    std::optional<TerrainSurfaceWeatherFrame> weather;
    render::TerrainSurfacePatchEnvironment environment;
    if (external_environment) {
        s.externally_lit = true;
        environment = *external_environment;
    } else if (s.weather && s.weather_enabled) {
        s.externally_lit = false;
        s.weather->ensure_resources(device, *s.gpu, slot.count, target, s.shaders);
        weather = s.weather->frame(device, vulkan::CommandRecorder(commands), slot, camera,
                                   s.scene.fallback_environment());
        environment = weather->environment;
    } else {
        s.externally_lit = false;
        // A UI toggle may enter the reference path after shared-only startup.
        ensure_resources(device, *s.gpu, slot.count, target);
        s.daylight.record_pending_update(vulkan::CommandRecorder(commands), slot);
        environment = {s.daylight.pbr_environment_bindings(s.scene.fallback_environment()),
                       s.daylight.lighting(),
                       s.material.daylight_exposure,
                       s.daylight.reflection_probe().sky_radiance_cube().sampler().handle(),
                       s.daylight.reflection_probe().sky_radiance_cube().view(),
                       render::atmosphere_environment_frame_uniforms(s.daylight.environment(), {})};
    }
    std::optional<render::TerrainSurfaceContextDraw> context;
    if (context_visible) {
        if (!s.context_resources)
            throw std::runtime_error("terrain context not prepared before recording");
        context = s.context_resources->draw();
    }
    std::optional<render::TerrainSurfacePatchMeshDraw> mesh;
    if (s.fixed_mesh) {
        if (!s.mesh_fields)
            throw std::runtime_error("fixed paired mesh was not uploaded before recording");
        mesh.emplace(render::TerrainSurfacePatchMeshDraw{
            .coordinates = &*s.mesh_coordinates,
            .indices = &*s.mesh_indices,
            .descriptor_set = s.mesh_fields->set(),
            .descriptor_layout = s.mesh_fields->layout(),
            .fields_layout = s.fields->layout(),
            .index_count = std::uint32_t(s.fixed_mesh->indices.size()),
            .fine_minimum = s.fixed_mesh->fine_minimum_world - s.capture.grid.center_xz_m(),
            .fine_maximum = s.fixed_mesh->fine_maximum_world - s.capture.grid.center_xz_m()});
    }
    if (fixed_lod && !mesh)
        throw std::runtime_error("fixed paired mesh not prepared");
    s.scene.record(
        device, commands, s.graph, slot, target, present, s.field_view(), s.grid(), camera,
        {.terrain_thin_water_composite = true,
         .native_shadow_extent = context_visible || mesh ? 4096U : 2048U,
         .water_visible = water,
         .fixed_mesh = fixed_lod ? &*mesh : nullptr,
         .fixed_shadow_region = mesh ? &*mesh : nullptr},
        seconds * 0.03, s.material, {}, profile, false, ground_view, 0U, 3U, &environment,
        {0U, seconds, seconds, s.capture.rain_mm_per_hour}, s.whitewater.seeds, 0U,
        context ? &*context : nullptr, weather ? &*weather : nullptr, foreground);
}
void TerrainWaterCaptureRuntime::enable_environment(TerrainSurfaceEnvironmentSettings settings) {
    if (!state_->weather)
        state_->weather = std::make_unique<TerrainSurfaceEnvironment>(std::move(settings));
    state_->weather_enabled = true;
}
bool& TerrainWaterCaptureRuntime::environment_enabled() {
    return state_->weather_enabled;
}
TerrainSurfaceEnvironment* TerrainWaterCaptureRuntime::environment() {
    return state_->weather.get();
}
void TerrainWaterCaptureRuntime::advance_environment(double delta) {
    if (state_->weather && state_->weather_enabled)
        state_->weather->advance(delta);
}
void TerrainWaterCaptureRuntime::prepare_fixed_lod(math::Vec2 focus, float span,
                                                   std::uint32_t maximum_vertices) {
    auto& s = *state_;
    if (s.fixed_mesh)
        return;
    const auto started = std::chrono::steady_clock::now();
    s.fixed_mesh.emplace(
        terrain::make_terrain_surface_patch_lod(s.capture.grid, focus, span, maximum_vertices));
    std::printf(
        "terrain_fixed_lod: CPU once; build_ms=%.3f fine_span_m=%.3f focus_world=(%.3f,%.3f)\n",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count(),
        span, focus.x, focus.y);
}
void TerrainWaterCaptureRuntime::set_terrain_readability(unsigned mode) {
    if (mode > 3U)
        throw std::runtime_error("invalid terrain readability mode");
    auto& s = *state_;
    s.material.terrain_readability = float(mode);
    const auto center = s.capture.grid.center_xz_m();
    s.material.terrain_world_x_m = center.x;
    s.material.terrain_world_z_m = center.y;
}
void TerrainWaterCaptureRuntime::set_shadow_strength(float strength) {
    if (!std::isfinite(strength) || strength < 0 || strength > 1)
        throw std::runtime_error("capture shadow strength must be in [0,1]");
    state_->material.shadow_strength = strength;
}
void TerrainWaterCaptureRuntime::prepare_context(const std::filesystem::path& extension,
                                                 float maximum_spacing) {
    auto& s = *state_;
    if (s.context)
        return;
    const auto started = std::chrono::steady_clock::now();
    std::optional<asset::TerrainRasterHeightSource> wider;
    if (!extension.empty()) {
        wider.emplace(extension);
        validate_extension(s.capture, s.source, *wider);
    }
    const auto& source = wider ? *wider : s.source;
    std::optional<asset::TerrainRasterClimateSource> climate;
    const auto companion = source.provenance().manifest_path.parent_path() / "surface-fields.json";
    if (std::filesystem::exists(companion)) {
        climate.emplace(companion);
        asset::validate_terrain_climate_binding(source, *climate);
    }
    const auto field =
        terrain::make_terrain_surface_field(source, source.bounds(), climate ? &*climate : nullptr);
    if (wider || s.terrain_only)
        s.context.emplace(terrain::make_terrain_surface_context(
            terrain::TerrainSurfaceContextInput{s.capture.grid, s.capture.bed_m,
                                                s.terrain_only
                                                    ? std::span<const float>{}
                                                    : std::span<const float>{s.capture.depth_m}},
            source, field, 128.0F, maximum_spacing));
    else
        s.context.emplace(terrain::make_terrain_surface_context(s.capture, source, field, 128.0F,
                                                                maximum_spacing));
    std::printf(
        "terrain_context: ground-only; vertices=%zu triangles=%zu perimeter=%u; build_ms=%.3f; "
        "water_continuation=none far_shadows=unqualified\n",
        s.context->vertices.size(), s.context->indices.size() / 3,
        s.context->inner_perimeter_vertices,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count());
}
const terrain::TerrainSurfaceContextProduct* TerrainWaterCaptureRuntime::context_product() const {
    return state_->context ? &*state_->context : nullptr;
}
void TerrainWaterCaptureRuntime::destroy_target_resources() {
    if (state_) {
        state_->scene.destroy_swapchain_resources();
        state_->graph.clear();
    }
}
void TerrainWaterCaptureRuntime::destroy() {
    state_.reset();
}
std::vector<vulkan::GpuPassTiming> TerrainWaterCaptureRuntime::collect_timings(std::uint32_t slot) {
    auto timings = state_->scene.collect_timings(slot);
    if (state_->whole_scene_profiler) {
        state_->whole_scene_profiler->collect(slot);
        const auto& whole = state_->whole_scene_profiler->latest_timings();
        timings.insert(timings.end(), whole.begin(), whole.end());
    }
    return timings;
}
const asset::TerrainWaterCapture& TerrainWaterCaptureRuntime::capture() const {
    return state_->capture;
}
std::string TerrainWaterCaptureRuntime::source_label() const {
    return std::string(state_->source.metadata().id);
}
VkDeviceSize TerrainWaterCaptureRuntime::retained_gpu_field_bytes() const {
    const auto& s = *state_;
    return s.bed ? s.bed->size() + s.depth->size() + s.velocity->size() + s.zeros->size() +
                       s.coverage->size()
                 : 0U;
}
void TerrainWaterCaptureRuntime::verify_immutable_fields(vulkan::GpuRuntime& gpu) const {
    const auto& s = *state_;
    if (!s.fields)
        return;
    for (const auto& [buffer, expected] :
         std::array<std::pair<const vulkan::Buffer*, std::span<const std::byte>>, 3>{
             {{&*s.bed, std::as_bytes(std::span{s.capture.bed_m})},
              {&*s.depth, std::as_bytes(std::span{s.capture.depth_m})},
              {&*s.velocity, std::as_bytes(std::span{s.velocity_values})}}}) {
        std::vector<std::byte> actual(expected.size());
        // Explicitly outside frame profiling and after the caller's retirement wait.
        (void)gpu.submit_and_wait(
            {.label = "captured water immutable readback", .work = [&](auto& context) {
                 vulkan::Buffer readback(context.device(),
                                         vulkan::readback_buffer_config(buffer->size()));
                 vulkan::copy_buffer(context, buffer->handle(), readback.handle(), buffer->size());
                 readback.download(actual.data(), actual.size());
             }});
        if (asset::sha256_hex(actual) != asset::sha256_hex(expected))
            throw std::runtime_error("captured water immutable GPU fields changed");
    }
    std::printf("terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact; "
                "hydraulic_dispatches=0; no radial_ground; environment=%s\n",
                s.externally_lit                 ? "caller-owned"
                : s.weather && s.weather_enabled ? "shared"
                                                 : "fixed");
}
} // namespace cubey
