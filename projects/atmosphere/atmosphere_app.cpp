#include "atmosphere_app.h"

#include "atmosphere_environment.h"
#include "atmosphere_ui.h"

#include <cubey/core/jobs.h>
#include <cubey/core/math.h>
#include <cubey/core/profiling.h>
#include <cubey/engine/cloud_environment_runtime.h>
#include <cubey/engine/staged_resource.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/procedural/artifact_cache.h>
#include <cubey/render/atmosphere_atlas_cache.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_night_sky_atlas.h>
#include <cubey/render/celestial_body_frame.h>
#include <cubey/render/celestial_system.h>
#include <cubey/render/cloud_layer.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/lunar_surface_map.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef CUBEY_ATMOSPHERE_SHADER_DIR
#error "CUBEY_ATMOSPHERE_SHADER_DIR must be defined by the atmosphere CMake target"
#endif

namespace cubey::projects::atmosphere {
namespace {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;
using cubey::render::LunarSurfaceMap;
using cubey::render::NightSkyAtlas;
using cubey::render::NightSkyAtlasConfig;

constexpr float kBaseYaw = cubey::render::kAtmosphereEnvironmentSunriseViewYawRadians;
constexpr float kBasePitch = cubey::render::kAtmosphereEnvironmentSunriseViewPitchRadians;
constexpr float kDefaultFovyRadians = 65.0F * (std::numbers::pi_v<float> / 180.0F);
constexpr VkFormat kAtmosphereSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr std::uint32_t kAtmosphereGpuProfilerPassCapacity = 16U;

[[nodiscard]] std::uint64_t profile_frame_index(std::uint64_t frame_index) {
    return frame_index == 0 ? 0 : frame_index - 1U;
}

[[nodiscard]] std::uint64_t collected_profile_frame_index(std::uint64_t frame_index,
                                                          cubey::render::FrameSlot frame_slot) {
    if (frame_index > frame_slot.count) {
        return frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return profile_frame_index(frame_index);
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

struct ResolvedNightSkyAtlas {
    float procedural_variation = 0.0F;
    NightSkyLayerView layer = NightSkyLayerView::Final;
};

struct GeneratedNightSkyAtlas {
    ResolvedNightSkyAtlas resolved{};
    NightSkyAtlas atlas{};
    cubey::render::AtmosphereAtlasCacheDiagnostics cache{};
};

struct PreparedAtmosphereAtlases {
    GeneratedNightSkyAtlas night_sky{};
    std::optional<LunarSurfaceMap> lunar_surface{};
    std::optional<cubey::render::AtmosphereAtlasCacheDiagnostics> lunar_surface_cache{};
    std::shared_ptr<cubey::render::Texture2D> retained_lunar_surface{};
};

struct AtmosphereAtlasResident {
    ResolvedNightSkyAtlas resolved{};
    std::shared_ptr<cubey::render::Texture2D> lunar_surface{};
    std::shared_ptr<cubey::render::TextureCube> night_sky{};
    std::shared_ptr<cubey::render::AtmosphereBackgroundFrameMaterial> background_material{};
    std::shared_ptr<cubey::render::CelestialBodyFrameMaterial> moon_material{};
    std::optional<cubey::render::AtmosphereAtlasCacheDiagnostics> night_sky_cache{};
    std::optional<cubey::render::AtmosphereAtlasCacheDiagnostics> lunar_surface_cache{};
    bool lunar_surface_placeholder = false;
    bool night_sky_placeholder = false;

    [[nodiscard]] cubey::render::AtmosphereBackgroundTextureBindings bindings() const {
        if (lunar_surface == nullptr || night_sky == nullptr) {
            throw std::runtime_error("atmosphere atlas generation is incomplete");
        }
        return {
            .lunar_surface_sampler = lunar_surface->sampler().handle(),
            .lunar_surface_view = lunar_surface->view(),
            .night_sky_sampler = night_sky->sampler().handle(),
            .night_sky_view = night_sky->view(),
        };
    }
};

struct CompiledAtmosphereGraph {
    cubey::render::CompiledRenderGraph graph{};
    cubey::render::RenderGraphTextureHandle post_scene_color{};
    cubey::render::RenderGraphTextureHandle atmosphere_scene_color{};
    cubey::render::RenderGraphTextureHandle cloud_scene_color{};
    cubey::render::CloudLayerRuntimeFrame cloud{};
    bool clouds_enabled = false;
};

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_ATMOSPHERE_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files() {
    return cubey::render::cloud_layer_runtime_shader_files(
        std::filesystem::path(CUBEY_ATMOSPHERE_SHADER_DIR),
        cubey::render::CloudLayerCompositeMode::ExternalBackground);
}

[[nodiscard]] ResolvedNightSkyAtlas resolve_night_sky_atlas(const AtmosphereConfig& config) {
    return {
        .procedural_variation = config.night_sky.procedural_variation,
        .layer = config.night_sky.layer,
    };
}

[[nodiscard]] bool same_night_sky_atlas(const ResolvedNightSkyAtlas& lhs,
                                        const ResolvedNightSkyAtlas& rhs) {
    return lhs.procedural_variation == rhs.procedural_variation && lhs.layer == rhs.layer;
}

[[nodiscard]] NightSkyAtlasConfig night_sky_atlas_config(const ResolvedNightSkyAtlas& resolved) {
    return {
        .procedural_variation = resolved.procedural_variation,
        .layer = resolved.layer,
    };
}

[[nodiscard]] GeneratedNightSkyAtlas
prepare_resolved_night_sky_atlas(cubey::procedural::ProceduralArtifactCache& cache,
                                 ResolvedNightSkyAtlas resolved) {
    cubey::render::PreparedNightSkyAtlas prepared =
        cubey::render::prepare_night_sky_atlas(cache, night_sky_atlas_config(resolved));
    return {
        .resolved = resolved,
        .atlas = std::move(prepared.atlas),
        .cache = std::move(prepared.cache),
    };
}

class AtmosphereApp {
  public:
    explicit AtmosphereApp(AtmosphereProjectConfig config)
        : config_(std::move(config)),
          atmosphere_config_(config_.atmosphere),
          render_view_(atmosphere_config_.render_view),
          headless_base_time_hours_(atmosphere_config_.time_of_day.time_hours),
          headless_base_day_of_year_(atmosphere_config_.time_of_day.day_of_year),
          atlas_cache_({.root = cubey::procedural::default_procedural_artifact_cache_root()}),
          atlas_builds_(atlas_jobs_) {
        view_controller_.set_auto_rotation_speed(0.0F);
    }

    AtmosphereApp(const AtmosphereApp&) = delete;
    AtmosphereApp& operator=(const AtmosphereApp&) = delete;

    ~AtmosphereApp() = default;

    int run() {
        request_night_sky_atlas_if_needed(desired_windowed_night_sky_atlas());
        if (config_.common.headless) {
            return run_headless();
        }
        return run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_windowed_global_resources(context.device(), context.gpu(),
                                             context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_windowed(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) {
            refresh_loading_status();
            draw_atmosphere_ui({
                .config = atmosphere_config_,
                .performance =
                    {
                        .frame_stats = latest_frame_stats_,
                        .latest_fps = latest_fps_,
                        .latest_frame_ms = latest_frame_ms_,
                        .process = process_stats_.sample(),
                        .device_memory_budget = context.device().device_memory_budget(),
                        .gpu_timings = latest_gpu_timings(),
                    },
                .render_view = render_view_,
                .reset_requested = reset_requested_,
                .loading_status = loading_status_,
            });
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            refresh_cloud_weather_if_needed(context.device(), context.gpu());
            record_windowed_frame(context.device(), context.profile_recorder(), frame);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            return record_frame_stats(context.swapchain().extent(), timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            destroy_swapchain_resources();
            destroy_global_resources(context.gpu());
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_.common,
                .app_name = "atmosphere",
                .ready_status = "rendering atmosphere project",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_.common;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            const std::uint32_t frame_slot_count =
                cubey::host::headless_capture_frame_slot_count(context.config());
            create_windowed_global_resources(context.device(), context.gpu(), frame_slot_count);
            atlas_builds_.finish(context.gpu());
            install_ready_atlas_generation(context.gpu(), {});
            record_initialization_metrics(context.profile_recorder(), 0U);
            create_pipeline(context.device(), target.format, target.extent);
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            update_headless_time(frame);
            refresh_cloud_weather_if_needed(context.device(), context.gpu());
            record_atmosphere_target(context.device(), command_buffer, target, frame.frame_slot);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext& context) {
            destroy_swapchain_resources();
            destroy_global_resources(context.gpu());
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update_windowed(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_requested_ = true;
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            render_view_ = next_atmosphere_render_view(render_view_);
        }
        if (input.key_pressed(cubey::input::Key::Space) &&
            atmosphere_config_.time_of_day.mode == SunControlMode::SolarClock) {
            atmosphere_config_.time_of_day.playing = !atmosphere_config_.time_of_day.playing;
        }
        view_controller_.update_pointer_input(input, timing.delta_seconds);
        if (reset_requested_) {
            const AtmospherePreset preset = atmosphere_config_.preset;
            atmosphere_config_ = atmosphere_config_for_preset(preset);
            headless_base_time_hours_ = atmosphere_config_.time_of_day.time_hours;
            headless_base_day_of_year_ = atmosphere_config_.time_of_day.day_of_year;
            render_view_ = atmosphere_config_.render_view;
            view_controller_.reset();
            failed_night_sky_atlas_.reset();
            atlas_activation_error_.clear();
            reset_requested_ = false;
        }
        advance_atmosphere_time_of_day(atmosphere_config_, timing.delta_seconds);
        cloud_runtime_.advance(timing.delta_seconds);
        resolve_atmosphere_time_of_day(atmosphere_config_);
        atmosphere_config_.render_view = render_view_;
        validate_atmosphere_config(atmosphere_config_);
        refresh_async_atlases_if_needed(context.gpu(),
                                        context.frame_resources().latest_submitted_ticket());
        refresh_loading_status();
    }

    void update_headless_time(const cubey::host::HeadlessCaptureFrame& frame) {
        if (config_.common.capture_mode == CaptureMode::Video &&
            atmosphere_config_.time_of_day.mode == SunControlMode::SolarClock &&
            atmosphere_config_.time_of_day.playing) {
            set_atmosphere_time_from_elapsed(atmosphere_config_.time_of_day,
                                             headless_base_time_hours_, headless_base_day_of_year_,
                                             frame.timing.elapsed_seconds);
        }
        cloud_runtime_.set_elapsed_seconds(frame.timing.elapsed_seconds);
        resolve_atmosphere_time_of_day(atmosphere_config_);
        validate_atmosphere_config(atmosphere_config_);
    }

    std::optional<FrameStatsSample> record_frame_stats(VkExtent2D extent,
                                                       const FrameTiming& timing) {
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = 1,
        };
        if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    [[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>& latest_gpu_timings() const {
        if (!gpu_profiler_.has_value()) {
            static const std::vector<cubey::vulkan::GpuPassTiming> empty;
            return empty;
        }
        return gpu_profiler_->latest_timings();
    }

    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* gpu_profiler() {
        return gpu_profiler_.has_value() ? &gpu_profiler_.value() : nullptr;
    }

    void record_initialization_metrics(cubey::profiling::ProfileRecorder* profile_recorder,
                                       std::uint64_t frame_index) const {
        if (profile_recorder == nullptr) {
            return;
        }
        const cubey::StagedResourceStatus& status = atlas_builds_.status();
        profile_recorder->record_metric(frame_index, "atmosphere.initialization", "generation",
                                        static_cast<double>(status.generation.id));
        profile_recorder->record_metric(
            frame_index, "atmosphere.initialization", "phase",
            static_cast<double>(static_cast<std::uint8_t>(status.phase)));
        profile_recorder->record_metric(frame_index, "atmosphere.initialization", "prepare_ms",
                                        status.prepare_milliseconds);
        profile_recorder->record_metric(frame_index, "atmosphere.initialization", "install_ms",
                                        status.install_milliseconds);
        const auto record_cache =
            [profile_recorder, frame_index](
                std::string_view prefix,
                const std::optional<cubey::render::AtmosphereAtlasCacheDiagnostics>& cache) {
                if (!cache.has_value()) {
                    return;
                }
                const std::string name_prefix{prefix};
                profile_recorder->record_metric(
                    frame_index, "atmosphere.initialization", name_prefix + "_cache_hit",
                    cache->source == cubey::render::AtmosphereAtlasPreparationSource::Cache ? 1.0
                                                                                            : 0.0);
                profile_recorder->record_metric(frame_index, "atmosphere.initialization",
                                                name_prefix + "_cache_load_ms",
                                                cache->load_milliseconds);
                profile_recorder->record_metric(frame_index, "atmosphere.initialization",
                                                name_prefix + "_generation_ms",
                                                cache->generation_milliseconds);
                profile_recorder->record_metric(frame_index, "atmosphere.initialization",
                                                name_prefix + "_cache_store_ms",
                                                cache->store_milliseconds);
            };
        record_cache("night_sky", loading_status_.night_sky_cache);
        record_cache("moon", loading_status_.moon_cache);
    }

    void collect_gpu_timings(cubey::profiling::ProfileRecorder* profile_recorder,
                             std::uint64_t frame_index, cubey::render::FrameSlot frame_slot) {
        cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler();
        if (profiler == nullptr) {
            return;
        }
        profiler->collect(frame_slot.index);
        const std::uint64_t collected_frame =
            collected_profile_frame_index(frame_index, frame_slot);
        record_gpu_timings(profile_recorder, collected_frame, latest_gpu_timings());
        record_initialization_metrics(profile_recorder, collected_frame);
    }

    void create_windowed_global_resources(cubey::vulkan::Device& device,
                                          cubey::vulkan::GpuRuntime& gpu,
                                          std::uint32_t frame_slot_count) {
        atlas_frame_slot_count_ = frame_slot_count;
        loading_status_.moon_cache.reset();
        loading_status_.night_sky_cache.reset();
        active_atlas_generation_ =
            create_placeholder_atlas_generation(device, gpu, frame_slot_count);
        atmosphere_background_.install_materials(active_atlas_generation_->background_material);
        moon_body_frame_.install_materials(active_atlas_generation_->moon_material);
        create_cloud_resources(device, gpu);
        current_night_sky_atlas_.reset();
        hdr_post_frame_.create_materials(device, {.frame_slot_count = frame_slot_count});
        graph_executor_.resize(frame_slot_count);
        gpu_profiler_.emplace(device, frame_slot_count, kAtmosphereGpuProfilerPassCapacity);
        create_moon_mesh_if_needed(gpu);
        refresh_loading_status();
    }

    [[nodiscard]] cubey::CloudEnvironmentConfig atmosphere_cloud_config() const {
        cubey::CloudEnvironmentConfig config = atmosphere_config_.clouds;
        config.layer.planet_radius_m = atmosphere_config_.bottom_radius_km * 1000.0F;
        config.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
        return config;
    }

    void create_cloud_resources(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
        if (cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.create_surface_resources(device, gpu, cloud_runtime_shader_files().generated,
                                                atmosphere_cloud_config());
        cloud_global_resources_created_ = true;
    }

    void refresh_cloud_weather_if_needed(cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu) {
        if (!cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.set_config(atmosphere_cloud_config());
        cloud_runtime_.update_weather_texture(device, gpu,
                                              cloud_runtime_shader_files().generated.weather);
    }

    [[nodiscard]] static cubey::render::CelestialBodyFrameTextureBindings
    moon_bindings(const cubey::render::AtmosphereBackgroundTextureBindings& textures) {
        return {
            .surface_sampler = textures.lunar_surface_sampler,
            .surface_view = textures.lunar_surface_view,
            .surface_layout = textures.lunar_surface_layout,
        };
    }

    [[nodiscard]] static std::shared_ptr<AtmosphereAtlasResident>
    create_placeholder_atlas_generation(cubey::vulkan::Device& device,
                                        cubey::vulkan::GpuRuntime& gpu,
                                        std::uint32_t frame_slot_count) {
        cubey::render::AtmosphereBackgroundAtlasResources textures =
            cubey::render::create_atmosphere_background_placeholder_textures(device, gpu);
        auto resident = std::make_shared<AtmosphereAtlasResident>();
        resident->lunar_surface =
            std::make_shared<cubey::render::Texture2D>(std::move(textures.lunar_surface));
        resident->night_sky =
            std::make_shared<cubey::render::TextureCube>(std::move(textures.night_sky));
        resident->lunar_surface_placeholder = true;
        resident->night_sky_placeholder = true;
        const cubey::render::AtmosphereBackgroundTextureBindings bindings = resident->bindings();
        resident->background_material = cubey::render::create_atmosphere_background_frame_material(
            device, {.frame_slot_count = frame_slot_count, .textures = bindings});
        resident->moon_material = cubey::render::create_celestial_body_frame_material(
            device, {.frame_slot_count = frame_slot_count, .textures = moon_bindings(bindings)});
        return resident;
    }

    [[nodiscard]] std::shared_ptr<AtmosphereAtlasResident>
    create_resident_atlas_generation(cubey::vulkan::GpuOwnerContext& owner,
                                     PreparedAtmosphereAtlases prepared) const {
        if (atlas_frame_slot_count_ == 0U) {
            throw std::runtime_error("atmosphere atlas frame slots are not initialized");
        }
        auto resident = std::make_shared<AtmosphereAtlasResident>();
        resident->resolved = prepared.night_sky.resolved;
        resident->night_sky_cache.emplace(std::move(prepared.night_sky.cache));
        resident->lunar_surface_cache = std::move(prepared.lunar_surface_cache);
        if (prepared.retained_lunar_surface != nullptr) {
            resident->lunar_surface = std::move(prepared.retained_lunar_surface);
        } else if (prepared.lunar_surface.has_value()) {
            resident->lunar_surface = std::make_shared<cubey::render::Texture2D>(
                cubey::render::create_lunar_surface_map_texture(owner.device(), owner,
                                                                prepared.lunar_surface.value()));
        } else {
            throw std::runtime_error("atmosphere atlas generation has no lunar surface source");
        }
        resident->night_sky = std::make_shared<cubey::render::TextureCube>(
            cubey::render::create_atmosphere_night_sky_atlas_texture(owner.device(), owner,
                                                                     prepared.night_sky.atlas));
        const cubey::render::AtmosphereBackgroundTextureBindings bindings = resident->bindings();
        resident->background_material = cubey::render::create_atmosphere_background_frame_material(
            owner.device(), {.frame_slot_count = atlas_frame_slot_count_, .textures = bindings});
        resident->moon_material = cubey::render::create_celestial_body_frame_material(
            owner.device(),
            {.frame_slot_count = atlas_frame_slot_count_, .textures = moon_bindings(bindings)});
        return resident;
    }

    void install_ready_atlas_generation(cubey::vulkan::GpuRuntime& gpu,
                                        cubey::vulkan::GpuSubmissionTicket retire_after) {
        if (!atlas_builds_.ready()) {
            return;
        }
        auto result = atlas_builds_.take_ready();
        std::shared_ptr<AtmosphereAtlasResident> next = std::move(result.resident);
        if (next == nullptr) {
            throw std::runtime_error("atmosphere atlas resident generation is invalid");
        }

        std::shared_ptr<AtmosphereAtlasResident> retired = active_atlas_generation_;
        if (retired != nullptr) {
            gpu.defer_destruction_after(retire_after, [retired = std::move(retired)] {});
        }
        atmosphere_background_.install_materials(next->background_material);
        moon_body_frame_.install_materials(next->moon_material);
        active_atlas_generation_ = std::move(next);
        current_night_sky_atlas_ = active_atlas_generation_->resolved;
        requested_night_sky_atlas_.reset();
        failed_night_sky_atlas_.reset();
        atlas_activation_error_.clear();
        refresh_loading_status();
    }

    [[nodiscard]] ResolvedNightSkyAtlas desired_windowed_night_sky_atlas() const {
        return resolve_night_sky_atlas(atmosphere_config_);
    }

    void request_night_sky_atlas_if_needed(const ResolvedNightSkyAtlas& resolved) {
        if (!atlas_builds_.accepting()) {
            return;
        }
        if (current_night_sky_atlas_.has_value() &&
            same_night_sky_atlas(current_night_sky_atlas_.value(), resolved)) {
            return;
        }
        if (failed_night_sky_atlas_.has_value() &&
            same_night_sky_atlas(failed_night_sky_atlas_.value(), resolved)) {
            return;
        }
        if (requested_night_sky_atlas_.has_value() && atlas_builds_.busy() &&
            same_night_sky_atlas(requested_night_sky_atlas_.value(), resolved)) {
            return;
        }

        std::shared_ptr<cubey::render::Texture2D> retained_lunar_surface;
        if (active_atlas_generation_ != nullptr &&
            !active_atlas_generation_->lunar_surface_placeholder) {
            retained_lunar_surface = active_atlas_generation_->lunar_surface;
        }
        static_cast<void>(atlas_builds_.request(
            "atmosphere generated atlases",
            [this, resolved, retained_lunar_surface]() mutable {
                PreparedAtmosphereAtlases prepared{
                    .night_sky = prepare_resolved_night_sky_atlas(atlas_cache_, resolved),
                    .retained_lunar_surface = std::move(retained_lunar_surface),
                };
                if (prepared.retained_lunar_surface == nullptr) {
                    cubey::render::PreparedLunarSurfaceMap lunar_surface =
                        cubey::render::prepare_lunar_surface_map(atlas_cache_);
                    prepared.lunar_surface.emplace(std::move(lunar_surface.map));
                    prepared.lunar_surface_cache.emplace(std::move(lunar_surface.cache));
                }
                return prepared;
            },
            [this](cubey::vulkan::GpuOwnerContext& owner, PreparedAtmosphereAtlases&& prepared) {
                return create_resident_atlas_generation(owner, std::move(prepared));
            }));
        requested_night_sky_atlas_ = resolved;
        failed_night_sky_atlas_.reset();
        atlas_activation_error_.clear();
    }

    void refresh_async_atlases_if_needed(cubey::vulkan::GpuRuntime& gpu,
                                         cubey::vulkan::GpuSubmissionTicket retire_after) {
        if (!atmosphere_background_.materials_created()) {
            return;
        }
        try {
            while (atlas_builds_.poll(gpu)) {
            }
            install_ready_atlas_generation(gpu, retire_after);
        } catch (const std::exception& error) {
            atlas_activation_error_ = error.what();
            failed_night_sky_atlas_ = requested_night_sky_atlas_;
            requested_night_sky_atlas_.reset();
        }
        if (atlas_builds_.status().phase == cubey::StagedResourcePhase::Failed &&
            requested_night_sky_atlas_.has_value()) {
            failed_night_sky_atlas_ = requested_night_sky_atlas_;
            requested_night_sky_atlas_.reset();
        }
        request_night_sky_atlas_if_needed(desired_windowed_night_sky_atlas());
    }

    void refresh_loading_status() {
        const cubey::StagedResourceStatus& build_status = atlas_builds_.status();
        const bool lunar_placeholder = active_atlas_generation_ == nullptr ||
                                       active_atlas_generation_->lunar_surface_placeholder;
        loading_status_.moon_pending = lunar_placeholder && atlas_builds_.busy();
        loading_status_.night_sky_pending = atlas_builds_.busy();
        loading_status_.moon_placeholder = lunar_placeholder;
        loading_status_.night_sky_placeholder = !current_night_sky_atlas_.has_value();
        if (active_atlas_generation_ != nullptr) {
            if (active_atlas_generation_->night_sky_cache.has_value()) {
                loading_status_.night_sky_cache = active_atlas_generation_->night_sky_cache;
            }
            if (active_atlas_generation_->lunar_surface_cache.has_value()) {
                loading_status_.moon_cache = active_atlas_generation_->lunar_surface_cache;
            }
        }
        loading_status_.generation = build_status.generation.id;
        loading_status_.prepare_milliseconds = build_status.prepare_milliseconds;
        loading_status_.install_milliseconds = build_status.install_milliseconds;
        if (atlas_builds_.busy() || build_status.phase == cubey::StagedResourcePhase::Failed) {
            loading_status_.phase = cubey::staged_resource_phase_name(build_status.phase);
        } else {
            loading_status_.phase.clear();
        }
        loading_status_.moon_error = build_status.error;
        loading_status_.night_sky_error = build_status.error;
        if (!atlas_activation_error_.empty()) {
            loading_status_.moon_error = atlas_activation_error_;
            loading_status_.night_sky_error = atlas_activation_error_;
        }
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::array<cubey::render::ShaderStageFile, 2> atmosphere_shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("atmosphere.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("atmosphere.frag.spv"),
            },
        };
        atmosphere_background_.create_pipeline(
            device, {
                        .extent = extent,
                        .color_format = kAtmosphereSceneColorFormat,
                        .shader_stage_files = atmosphere_shader_stage_files,
                    });
        create_moon_body_frame_pipeline(device, extent);
        create_cloud_pipelines(device, extent);

        const std::array<cubey::render::ShaderStageFile, 2> post_shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("forward_pbr_post.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("forward_pbr_post.frag.spv"),
            },
        };
        hdr_post_frame_.create_pipeline(device, {
                                                    .extent = extent,
                                                    .color_format = color_format,
                                                    .shader_stage_files = post_shader_stage_files,
                                                });
        pipeline_color_format_ = color_format;
    }

    void create_cloud_pipelines(const cubey::vulkan::Device& device, VkExtent2D extent) {
        cloud_runtime_.create_surface_target_resources(
            device, cloud_runtime_shader_files(),
            cubey::render::CloudLayerCompositeMode::ExternalBackground, kAtmosphereSceneColorFormat,
            extent, graph_executor_.frame_slot_count());
    }

    void create_moon_mesh_if_needed(cubey::vulkan::GpuRuntime& gpu) {
        if (moon_mesh_.has_value()) {
            return;
        }
        const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv>
            moon_mesh = cubey::render::make_uv_sphere_position_color_normal_uv_mesh({
                .radius = 1.0F,
                .latitude_segments = 32U,
                .longitude_segments = 64U,
                .color = {0.86F, 0.86F, 0.86F},
            });
        moon_mesh_.emplace(gpu, moon_mesh.mesh_config());
    }

    void create_moon_body_frame_pipeline(const cubey::vulkan::Device& device, VkExtent2D extent) {
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("celestial_body.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("celestial_body.frag.spv")),
        };
        moon_body_frame_.destroy_pipeline();
        moon_body_frame_.create_pipeline(
            device, {
                        .extent = extent,
                        .color_format = kAtmosphereSceneColorFormat,
                        .shader_stage_files = shaders,
                        .depth_mode = cubey::render::CelestialBodyDepthMode::None,
                    });
    }

    void destroy_swapchain_resources() {
        const std::uint32_t frame_slot_count = graph_executor_.frame_slot_count();
        graph_executor_.clear();
        if (frame_slot_count != 0U) {
            graph_executor_.resize(frame_slot_count);
        }
        hdr_post_frame_.destroy_pipeline();
        moon_body_frame_.destroy_pipeline();
        cloud_runtime_.destroy_surface_target_resources();
        atmosphere_background_.destroy_pipeline();
        pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    }

    void destroy_global_resources(cubey::vulkan::GpuRuntime& gpu) {
        atlas_builds_.shutdown(gpu);
        atlas_jobs_.shutdown();
        graph_executor_.clear();
        gpu_profiler_.reset();
        hdr_post_frame_.destroy();
        cloud_runtime_.destroy();
        cloud_global_resources_created_ = false;
        moon_body_frame_.destroy();
        atmosphere_background_.destroy();
        moon_mesh_.reset();
        active_atlas_generation_.reset();
        current_night_sky_atlas_.reset();
        requested_night_sky_atlas_.reset();
        failed_night_sky_atlas_.reset();
        atlas_frame_slot_count_ = 0U;
        refresh_loading_status();
    }

    [[nodiscard]] AtmosphereFrameUniforms frame_uniforms(VkExtent2D extent) const {
        return atmosphere_frame_uniforms(atmosphere_background_config(),
                                         {
                                             .view_rays = atmosphere_view_rays(extent),
                                             .render_view = background_render_view(),
                                         });
    }

    [[nodiscard]] cubey::render::ViewRayBasis3D atmosphere_view_rays(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        return cubey::render::view_ray_basis_3d(atmosphere_view_rotation(), aspect,
                                                kDefaultFovyRadians);
    }

    [[nodiscard]] cubey::CloudEnvironmentRuntimeFrame current_cloud_frame(VkExtent2D extent) const {
        const cubey::render::ViewRayBasis3D view_rays = atmosphere_view_rays(extent);
        const cubey::render::AtmosphereEnvironmentConfig environment =
            atmosphere_environment_config(atmosphere_config_);
        const cubey::render::AtmosphereEnvironmentLighting lighting =
            cubey::render::atmosphere_environment_lighting(environment);
        const cubey::math::Vec3 cloud_camera_position{
            0.0F,
            atmosphere_config_.camera_altitude_km * 1000.0F,
            0.0F,
        };
        return cloud_runtime_.frame(
            cubey::CloudEnvironmentSurfaceViewInfo{
                .camera_position = cloud_camera_position,
                .camera_right = cubey::math::Vec3{view_rays.right_aspect},
                .camera_up = cubey::math::Vec3{view_rays.up_tan_half_fovy},
                .camera_forward = cubey::math::Vec3{view_rays.forward},
                .tan_half_fovy = view_rays.up_tan_half_fovy.w,
                .target_extent = extent,
                .external_background = true,
            },
            lighting);
    }

    [[nodiscard]] cubey::render::PbrPostUniforms post_uniforms() const {
        const float exposure =
            render_view_ == AtmosphereRenderView::MoonSurface ? 0.0F : atmosphere_config_.exposure;
        return cubey::render::hdr_post_uniforms(pipeline_color_format_, exposure);
    }

    void record_atmosphere_scene_pass(const cubey::vulkan::CommandRecorder& recorder,
                                      const cubey::render::ColorTargetView& target,
                                      cubey::render::FrameSlot frame_slot) const {
        atmosphere_background_.upload(frame_slot, frame_uniforms(target.extent));
        atmosphere_background_.record_pass(recorder, target, frame_slot);
    }

    [[nodiscard]] bool cloud_layer_enabled() const noexcept {
        return render_view_ == AtmosphereRenderView::Final && atmosphere_config_.clouds.enabled;
    }

    void record_atmosphere_post_pass(const cubey::vulkan::CommandRecorder& recorder,
                                     const cubey::render::ColorTargetView& target,
                                     cubey::render::FrameSlot frame_slot) const {
        hdr_post_frame_.record_pass(recorder, target, frame_slot);
    }

    [[nodiscard]] AtmosphereConfig atmosphere_background_config() const {
        AtmosphereConfig config = atmosphere_config_;
        config.render_moon_disk = false;
        return config;
    }

    [[nodiscard]] AtmosphereRenderView background_render_view() const {
        if (render_view_ == AtmosphereRenderView::MoonSurface) {
            return AtmosphereRenderView::Moon;
        }
        return render_view_;
    }

    [[nodiscard]] cubey::Transform3D atmosphere_camera_transform() const {
        return {
            .rotation = atmosphere_view_rotation(),
        };
    }

    [[nodiscard]] cubey::math::Quat atmosphere_view_rotation() const {
        const float yaw_offset =
            atmosphere_degrees_to_radians(atmosphere_config_.camera_yaw_offset_degrees);
        const float pitch_offset =
            atmosphere_degrees_to_radians(atmosphere_config_.camera_pitch_offset_degrees);
        return cubey::math::angle_axis_quat(kBaseYaw + yaw_offset + view_controller_.yaw(),
                                            {0.0F, 1.0F, 0.0F}) *
               cubey::math::angle_axis_quat(kBasePitch + pitch_offset + view_controller_.pitch(),
                                            {1.0F, 0.0F, 0.0F});
    }

    [[nodiscard]] bool moon_body_render_enabled() const {
        const cubey::render::AtmosphereEnvironmentConfig environment_config =
            atmosphere_environment_config(atmosphere_config_);
        const cubey::render::AtmosphereEnvironmentLunarState moon =
            cubey::render::atmosphere_environment_lunar_state(environment_config.time_of_day,
                                                              environment_config.moon);
        const bool moon_enabled = atmosphere_config_.render_celestial_content &&
                                  atmosphere_config_.render_moon_disk &&
                                  atmosphere_config_.moon.enabled;
        if (!moon_enabled) {
            return false;
        }
        if (render_view_ == AtmosphereRenderView::Final) {
            return moon.direction.y > -moon.angular_radius;
        }
        return render_view_ == AtmosphereRenderView::Moon ||
               render_view_ == AtmosphereRenderView::MoonSurface;
    }

    [[nodiscard]] cubey::render::CelestialBodyFrameUniforms
    moon_body_frame_uniforms(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const cubey::Transform3D transform = atmosphere_camera_transform();
        const cubey::render::AtmosphereEnvironmentConfig environment_config =
            atmosphere_environment_config(atmosphere_config_);
        const cubey::render::AtmosphereEnvironmentLunarState lunar =
            cubey::render::atmosphere_environment_lunar_state(environment_config.time_of_day,
                                                              environment_config.moon);
        const cubey::math::Vec3 camera_forward =
            glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});
        const cubey::math::Vec3 camera_right =
            glm::normalize(transform.rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F});
        const bool moon_debug = render_view_ == AtmosphereRenderView::Moon;
        const bool surface_debug = render_view_ == AtmosphereRenderView::MoonSurface;
        const bool framed_moon_debug = moon_debug || surface_debug;
        const float phase_angle = lunar.phase_fraction * 2.0F * std::numbers::pi_v<float>;
        const cubey::math::Vec3 debug_light_direction = glm::normalize(
            -camera_forward * std::cos(phase_angle) + camera_right * std::sin(phase_angle));
        cubey::render::CelestialBody moon{};
        moon.type = cubey::render::CelestialBodyType::Moon;
        moon.visible = moon_body_render_enabled();
        moon.direction = framed_moon_debug ? camera_forward : lunar.direction;
        moon.color = cubey::render::kCelestialMoonSurfaceColor;
        moon.intensity = atmosphere_config_.moon.disk_intensity;
        moon.angular_radius_rad =
            surface_debug ? 0.34F : (moon_debug ? 0.12F : lunar.angular_radius);
        moon.distance_m = 384400000.0F;
        moon.radius_m = 1737400.0F;
        moon.phase_fraction = lunar.phase_fraction;

        const cubey::render::CelestialBodyRenderPlacement placement =
            cubey::render::celestial_body_render_placement(
                moon, {
                          .camera_render_position_m = transform.translation,
                          .near_plane_m = 0.1F,
                          .far_plane_m = 100.0F,
                          .angular_radius_scale = 1.0F,
                          .shell_distance_fraction = 0.62F,
                      });
        const cubey::render::CelestialLighting lighting{
            .primary_light_direction =
                moon_debug ? debug_light_direction : atmosphere_sun_direction(atmosphere_config_),
            .primary_light_color = {1.0F, 0.94F, 0.82F},
            .primary_light_intensity = atmosphere_config_.moon.disk_intensity,
            .primary_light_angular_radius_rad = atmosphere_config_.sun_angular_radius,
            .moon_light_direction = lunar.direction,
            .moon_light_color = cubey::render::kCelestialMoonSurfaceColor,
            .moon_light_intensity = atmosphere_config_.moon.moonlight_intensity,
        };
        cubey::Camera3D camera(
            {.fovy_radians = kDefaultFovyRadians, .near_z = 0.1F, .far_z = 100.0F});
        return cubey::render::celestial_body_frame_uniforms(
            moon, placement, lighting, camera.view_projection_matrix(transform, aspect),
            {
                .camera_render_position_m = transform.translation,
                .shading_mode = surface_debug
                                    ? cubey::render::CelestialBodyShadingMode::SurfaceDebug
                                    : cubey::render::CelestialBodyShadingMode::Lit,
                .surface_detail_strength = surface_debug ? 1.0F : 0.42F,
                .surface_texture_strength = 1.0F,
                .limb_strength = surface_debug ? 0.0F : 0.32F,
            });
    }

    void record_moon_body_frame(const cubey::vulkan::CommandRecorder& recorder,
                                const cubey::render::ColorTargetView& target,
                                cubey::render::FrameSlot frame_slot) const {
        if (!moon_body_render_enabled()) {
            return;
        }
        moon_body_frame_.upload(frame_slot, moon_body_frame_uniforms(target.extent));
        moon_body_frame_.record_pass(recorder, cubey::render::render_target_view(target),
                                     frame_slot, moon_mesh());
    }

    [[nodiscard]] CompiledAtmosphereGraph current_render_graph(
        cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
        cubey::render::RenderGraphTextureState target_initial_state,
        cubey::render::RenderGraphTextureState target_final_state,
        std::optional<cubey::render::CloudLayerFrameUniforms> cloud_uniforms) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "backbuffer", target, target_initial_state, target_final_state);
        const cubey::render::RenderGraphTextureHandle scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "atmosphere scene color", target.extent, kAtmosphereSceneColorFormat));
        cubey::render::RenderGraphTextureHandle post_scene_color = scene_color;
        cubey::render::RenderGraphTextureHandle cloud_scene_color{};
        cubey::render::CloudLayerRuntimeFrame cloud_frame{};

        graph.add_pass("atmosphere sky", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .material_pass(cubey::render::atmosphere_background_pass_info())
            .execute([this, scene_color,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView target =
                    cubey::render::resolved_color_target_view(context, scene_color);
                record_atmosphere_scene_pass(context.recorder(), target, frame_slot);
            });
        if (moon_body_render_enabled()) {
            graph.add_pass("atmosphere moon", cubey::render::RenderGraphQueueDomain::Graphics)
                .write_color(scene_color)
                .material_pass(cubey::render::celestial_body_pass_info(
                    cubey::render::CelestialBodyDepthMode::None))
                .execute([this, scene_color,
                          frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                    record_moon_body_frame(
                        context.recorder(),
                        cubey::render::resolved_color_target_view(context, scene_color),
                        frame_slot);
                });
        }
        const bool clouds_enabled = cloud_layer_enabled() && cloud_uniforms.has_value();
        if (clouds_enabled) {
            cloud_scene_color = graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "atmosphere cloud scene color", target.extent, kAtmosphereSceneColorFormat));
            const cubey::CloudEnvironmentRuntimeFrame runtime_frame =
                current_cloud_frame(target.extent);
            cloud_frame = cloud_runtime_.declare_surface_product(graph, frame_slot, runtime_frame);
            cloud_runtime_.declare_surface_composite(graph, cloud_scene_color, cloud_frame,
                                                     frame_slot, scene_color);
            post_scene_color = cloud_scene_color;
        }
        graph.add_pass("atmosphere post", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(post_scene_color)
            .write_color(backbuffer)
            .material_pass(cubey::render::pbr_post_pass_info())
            .execute([this, target,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_atmosphere_post_pass(context.recorder(), target, frame_slot);
            });

        return {
            .graph = graph.compile(),
            .post_scene_color = post_scene_color,
            .atmosphere_scene_color = scene_color,
            .cloud_scene_color = cloud_scene_color,
            .cloud = cloud_frame,
            .clouds_enabled = clouds_enabled,
        };
    }

    void update_post_descriptor(const cubey::vulkan::Device& device,
                                cubey::render::FrameSlot frame_slot,
                                const cubey::render::CompiledRenderGraph& graph,
                                const cubey::render::RenderGraphResourceSet& resources,
                                cubey::render::RenderGraphTextureHandle scene_color) const {
        hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, graph, resources,
                                                      scene_color);
    }

    void record_atmosphere_graph(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                                 cubey::render::ColorTargetView target,
                                 cubey::render::FrameSlot frame_slot,
                                 cubey::render::RenderGraphTextureState target_initial_state,
                                 cubey::render::RenderGraphTextureState target_final_state,
                                 cubey::render::RenderGraphCommandBufferMode command_buffer_mode) {
        cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler();
        if (profiler != nullptr) {
            profiler->collect(frame_slot.index);
        }
        hdr_post_frame_.upload(frame_slot, post_uniforms());
        std::optional<cubey::render::CloudLayerFrameUniforms> cloud_uniforms{};
        if (cloud_layer_enabled()) {
            cloud_uniforms = current_cloud_frame(target.extent).uniforms;
        }
        const CompiledAtmosphereGraph render_graph = current_render_graph(
            target, frame_slot, target_initial_state, target_final_state, cloud_uniforms);

        const auto prepare_graph_resources =
            [this, &device, frame_slot,
             &render_graph](const cubey::render::RenderGraphResourceSet& resources) {
                update_post_descriptor(device, frame_slot, render_graph.graph, resources,
                                       render_graph.post_scene_color);
                if (!render_graph.clouds_enabled) {
                    return;
                }
                cloud_runtime_.update_surface_descriptors(device, frame_slot, render_graph.graph,
                                                          resources, render_graph.cloud,
                                                          render_graph.atmosphere_scene_color);
            };

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
                    .label = "vkEndCommandBuffer atmosphere",
                    .command_buffer_mode =
                        cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                    .profiler = profiler,
                },
                render_graph.graph, prepare_graph_resources);
            recorder.end("vkEndCommandBuffer atmosphere");
            if (render_graph.clouds_enabled) {
                cloud_runtime_.complete_surface_frame(frame_slot, render_graph.cloud);
            }
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
                .label = "vkEndCommandBuffer atmosphere",
                .command_buffer_mode = command_buffer_mode,
                .profiler = profiler,
            },
            render_graph.graph, prepare_graph_resources);
        if (render_graph.clouds_enabled) {
            cloud_runtime_.complete_surface_frame(frame_slot, render_graph.cloud);
        }
    }

    void record_windowed_frame(cubey::vulkan::Device& device,
                               cubey::profiling::ProfileRecorder* profile_recorder,
                               const cubey::host::WindowedRenderFrame& frame) {
        collect_gpu_timings(profile_recorder, frame.timing.frame_index, frame.frame_slot);
        record_atmosphere_graph(device, frame.command_buffer, frame.color_target, frame.frame_slot,
                                cubey::render::render_graph_undefined_texture_state(),
                                cubey::render::render_graph_present_texture_state(),
                                cubey::render::RenderGraphCommandBufferMode::BeginAndEnd);
    }

    void record_atmosphere_target(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                                  const cubey::host::HeadlessRenderTarget& target,
                                  cubey::render::FrameSlot frame_slot) {
        record_atmosphere_graph(device, command_buffer, target, frame_slot,
                                cubey::render::render_graph_color_attachment_texture_state(),
                                cubey::render::render_graph_color_attachment_texture_state(),
                                cubey::render::RenderGraphCommandBufferMode::AlreadyRecording);
    }

    [[nodiscard]] const cubey::render::Mesh& moon_mesh() const {
        if (!moon_mesh_.has_value()) {
            throw std::runtime_error("atmosphere moon mesh is not initialized");
        }
        return moon_mesh_.value();
    }

    AtmosphereProjectConfig config_;
    AtmosphereConfig atmosphere_config_;
    AtmosphereRenderView render_view_ = AtmosphereRenderView::Final;
    cubey::OrbitController view_controller_{cubey::OrbitControllerConfig{
        .distance = 1.0F,
        .min_distance = 1.0F,
        .max_distance = 1.0F,
    }};
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    cubey::host::ProcessResourceStatsSampler process_stats_;
    AtmosphereLoadingStatus loading_status_{};
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    float headless_base_time_hours_ = 12.0F;
    float headless_base_day_of_year_ = 80.0F;
    cubey::jobs::JobSystem atlas_jobs_{2U};
    cubey::procedural::ProceduralArtifactCache atlas_cache_;
    cubey::StagedResource<PreparedAtmosphereAtlases, std::shared_ptr<AtmosphereAtlasResident>>
        atlas_builds_;
    std::uint32_t atlas_frame_slot_count_ = 0U;
    std::shared_ptr<AtmosphereAtlasResident> active_atlas_generation_{};
    std::optional<ResolvedNightSkyAtlas> current_night_sky_atlas_{};
    std::optional<ResolvedNightSkyAtlas> requested_night_sky_atlas_{};
    std::optional<ResolvedNightSkyAtlas> failed_night_sky_atlas_{};
    std::string atlas_activation_error_{};
    bool reset_requested_ = false;

    VkFormat pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    cubey::render::AtmosphereBackgroundFrame atmosphere_background_;
    cubey::render::CelestialBodyFrame moon_body_frame_;
    cubey::render::HdrPostFrame hdr_post_frame_;
    cubey::CloudEnvironmentRuntime cloud_runtime_{};
    bool cloud_global_resources_created_ = false;
    std::optional<cubey::render::Mesh> moon_mesh_;
    cubey::render::RenderGraphFrameExecutor graph_executor_{};
    std::optional<cubey::vulkan::GpuTimestampProfiler> gpu_profiler_{};
};

} // namespace

int run_atmosphere(const AtmosphereProjectConfig& config) {
    AtmosphereApp app(config);
    return app.run();
}

} // namespace cubey::projects::atmosphere
