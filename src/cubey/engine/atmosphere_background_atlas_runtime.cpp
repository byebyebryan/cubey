#include <cubey/engine/atmosphere_background_atlas_runtime.h>

#include <stdexcept>
#include <utility>

namespace cubey {

AtmosphereBackgroundAtlasRuntime::AtmosphereBackgroundAtlasRuntime()
    : AtmosphereBackgroundAtlasRuntime(
          {.root = procedural::default_procedural_artifact_cache_root()}) {}

AtmosphereBackgroundAtlasRuntime::AtmosphereBackgroundAtlasRuntime(
    procedural::ProceduralArtifactCacheConfig cache_config)
    : cache_(std::move(cache_config)), builds_(jobs_) {}

void AtmosphereBackgroundAtlasRuntime::create(
    const vulkan::Device& device, vulkan::GpuRuntime& gpu,
    const render::AtmosphereBackgroundGeneratedAtlasConfig& config) {
    if (created()) {
        throw std::runtime_error("atmosphere background atlas runtime is already created");
    }
    active_ = std::make_shared<ResidentAtlases>(ResidentAtlases{
        .resources = render::create_atmosphere_background_placeholder_textures(device, gpu),
    });
    request(config);
}

void AtmosphereBackgroundAtlasRuntime::request(
    const render::AtmosphereBackgroundGeneratedAtlasConfig& config) {
    if (!created()) {
        throw std::runtime_error("atmosphere background atlas runtime is not created");
    }
    static_cast<void>(builds_.request(
        "prepare atmosphere background atlases",
        [this, config] {
            return PreparedAtlases{
                .lunar_surface = render::prepare_lunar_surface_map(
                    cache_, config.lunar_surface_width, config.lunar_surface_height),
                .night_sky = render::prepare_night_sky_atlas(cache_, config.night_sky,
                                                             config.night_sky_extent),
            };
        },
        [](vulkan::GpuOwnerContext& owner, PreparedAtlases&& prepared) {
            return std::make_shared<ResidentAtlases>(ResidentAtlases{
                .resources =
                    {
                        .lunar_surface = render::create_lunar_surface_map_texture(
                            owner.device(), owner, prepared.lunar_surface.map),
                        .night_sky = render::create_atmosphere_night_sky_atlas_texture(
                            owner.device(), owner, prepared.night_sky.atlas),
                    },
                .lunar_surface_cache = std::move(prepared.lunar_surface.cache),
                .night_sky_cache = std::move(prepared.night_sky.cache),
            });
        }));
}

bool AtmosphereBackgroundAtlasRuntime::poll(vulkan::GpuRuntime& gpu,
                                            vulkan::GpuSubmissionTicket retire_after) {
    while (builds_.poll(gpu)) {
    }
    return install_ready(gpu, retire_after);
}

bool AtmosphereBackgroundAtlasRuntime::finish(vulkan::GpuRuntime& gpu,
                                              vulkan::GpuSubmissionTicket retire_after) {
    builds_.finish(gpu);
    return install_ready(gpu, retire_after);
}

void AtmosphereBackgroundAtlasRuntime::shutdown(vulkan::GpuRuntime& gpu) {
    builds_.shutdown(gpu);
    active_.reset();
    lunar_surface_cache_.reset();
    night_sky_cache_.reset();
}

bool AtmosphereBackgroundAtlasRuntime::created() const noexcept {
    return active_ != nullptr;
}

bool AtmosphereBackgroundAtlasRuntime::ready() const noexcept {
    return created() && !active_->resources.lunar_surface_placeholder &&
           !active_->resources.night_sky_placeholder;
}

render::AtmosphereBackgroundTextureBindings AtmosphereBackgroundAtlasRuntime::bindings() const {
    if (!created()) {
        throw std::runtime_error("atmosphere background atlas runtime is not created");
    }
    return active_->resources.bindings();
}

AtmosphereBackgroundAtlasRuntimeStatus AtmosphereBackgroundAtlasRuntime::status() const {
    return {
        .build = builds_.status(),
        .lunar_surface_cache = lunar_surface_cache_,
        .night_sky_cache = night_sky_cache_,
        .lunar_surface_placeholder = !created() || active_->resources.lunar_surface_placeholder,
        .night_sky_placeholder = !created() || active_->resources.night_sky_placeholder,
    };
}

bool AtmosphereBackgroundAtlasRuntime::install_ready(vulkan::GpuRuntime& gpu,
                                                     vulkan::GpuSubmissionTicket retire_after) {
    if (!builds_.ready()) {
        return false;
    }
    StagedResourceResult<Resident> result = builds_.take_ready();
    if (result.resident == nullptr) {
        throw std::runtime_error("prepared atmosphere background atlases are invalid");
    }

    Resident retired = std::move(active_);
    active_ = std::move(result.resident);
    lunar_surface_cache_ = active_->lunar_surface_cache;
    night_sky_cache_ = active_->night_sky_cache;
    if (retired != nullptr) {
        gpu.defer_destruction_after(retire_after, [retired = std::move(retired)] {});
    }
    return true;
}

} // namespace cubey
