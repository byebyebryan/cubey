#pragma once

#include <cubey/engine/staged_resource.h>
#include <cubey/procedural/artifact_cache.h>
#include <cubey/render/atmosphere_atlas_cache.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <filesystem>
#include <memory>
#include <optional>

namespace cubey {

struct AtmosphereBackgroundAtlasRuntimeStatus {
    StagedResourceStatus build{};
    std::optional<render::AtmosphereAtlasCacheDiagnostics> lunar_surface_cache{};
    std::optional<render::AtmosphereAtlasCacheDiagnostics> night_sky_cache{};
    bool lunar_surface_placeholder = false;
    bool night_sky_placeholder = false;
};

class AtmosphereBackgroundAtlasRuntime {
  public:
    AtmosphereBackgroundAtlasRuntime();
    explicit AtmosphereBackgroundAtlasRuntime(
        procedural::ProceduralArtifactCacheConfig cache_config);

    AtmosphereBackgroundAtlasRuntime(const AtmosphereBackgroundAtlasRuntime&) = delete;
    AtmosphereBackgroundAtlasRuntime& operator=(const AtmosphereBackgroundAtlasRuntime&) = delete;
    AtmosphereBackgroundAtlasRuntime(AtmosphereBackgroundAtlasRuntime&&) = delete;
    AtmosphereBackgroundAtlasRuntime& operator=(AtmosphereBackgroundAtlasRuntime&&) = delete;

    void create(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                const render::AtmosphereBackgroundGeneratedAtlasConfig& config = {});
    void request(const render::AtmosphereBackgroundGeneratedAtlasConfig& config);
    [[nodiscard]] bool poll(vulkan::GpuRuntime& gpu, vulkan::GpuSubmissionTicket retire_after = {});
    [[nodiscard]] bool finish(vulkan::GpuRuntime& gpu,
                              vulkan::GpuSubmissionTicket retire_after = {});
    void shutdown(vulkan::GpuRuntime& gpu);

    [[nodiscard]] bool created() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] render::AtmosphereBackgroundTextureBindings bindings() const;
    [[nodiscard]] AtmosphereBackgroundAtlasRuntimeStatus status() const;

  private:
    struct PreparedAtlases {
        render::PreparedLunarSurfaceMap lunar_surface{};
        render::PreparedNightSkyAtlas night_sky{};
    };

    struct ResidentAtlases {
        render::AtmosphereBackgroundAtlasResources resources;
        std::optional<render::AtmosphereAtlasCacheDiagnostics> lunar_surface_cache{};
        std::optional<render::AtmosphereAtlasCacheDiagnostics> night_sky_cache{};
    };

    using Resident = std::shared_ptr<ResidentAtlases>;

    [[nodiscard]] bool install_ready(vulkan::GpuRuntime& gpu,
                                     vulkan::GpuSubmissionTicket retire_after);

    jobs::JobSystem jobs_{1U};
    procedural::ProceduralArtifactCache cache_;
    StagedResource<PreparedAtlases, Resident> builds_;
    Resident active_{};
    std::optional<render::AtmosphereAtlasCacheDiagnostics> lunar_surface_cache_{};
    std::optional<render::AtmosphereAtlasCacheDiagnostics> night_sky_cache_{};
};

} // namespace cubey
