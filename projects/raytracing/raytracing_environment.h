#pragma once
#include "raytracing_scene.h"
#include <chrono>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/render/generated_ibl.h>
#include <glm/gtc/constants.hpp>

namespace cubey::projects::raytracing {
inline float shared_sun_azimuth_degrees(float legacy_radians) {
    // Legacy RT measures azimuth from +Z; the shared atmosphere measures from -Z.
    return glm::degrees(glm::pi<float>() - legacy_radians);
}
struct EnvironmentUniform {
    math::Vec4 direct_color{};
    std::array<math::Vec4, 9> irradiance{};
    math::Vec4 options{}, transport{};
    glm::ivec4 limits{};
    math::Vec4 glass{}, attenuation{}; // attenuation.w: guarded smooth dragon interface
};
static_assert(sizeof(EnvironmentUniform) == 240);
class Environment {
  public:
    Environment(vulkan::Device&, vulkan::GpuRuntime&, const Config&, std::uint32_t slots);
    ~Environment();
    EnvironmentUniform prepare(const Config&, VkCommandBuffer, render::FrameSlot, math::Vec3 eye,
                               math::Vec3& sun);
    void bind(const vulkan::Device&, VkDescriptorSet, std::uint32_t slot = 0) const;
    void resize_background(const vulkan::Device&, VkExtent2D);
    void record_background(VkCommandBuffer, render::FrameSlot, const math::Mat4& inverse_view,
                           float aspect, float tan_half_fovy);
    float solar_radius() const {
        return atmosphere_.environment().sun_angular_radius;
    }
    double setup_ms() const {
        return setup_ms_;
    }
    double probe_gpu_ms() const {
        return probe_gpu_ms_;
    }
    double pipeline_ms() const {
        return pipeline_ms_;
    }
    double fallback_ms() const {
        return fallback_ms_;
    }
    std::uint64_t generation() const;
    std::uint64_t texture_allocation_bytes(const vulkan::Device&) const;
    const render::GeneratedPbrEnvironment& fallback() const {
        return *fallback_;
    }

  private:
    AtmosphereEnvironmentRuntime atmosphere_;
    std::optional<render::AtmosphereBackgroundPlaceholderTextures> placeholders_;
    std::optional<render::GeneratedPbrEnvironment> fallback_;
    bool active_ = false;
    bool sampled_ = false;
    render::AtmosphereEnvironmentGroundMode probe_ground_mode_ =
        render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
    std::uint32_t slots_ = 1;
    render::AtmosphereBackgroundFrame background_;
    struct BackgroundTarget {
        std::unique_ptr<render::Texture2D> color;
        bool initialized = false;
    };
    std::vector<BackgroundTarget> background_targets_;
    std::unique_ptr<render::TextureCube> sun_component_;
    std::vector<vulkan::ImageView> sun_faces_;
    render::AtmosphereBackgroundFrame sun_background_;
    std::uint64_t sun_generation_ = UINT64_MAX;
    double setup_ms_ = 0;
    double probe_gpu_ms_ = 0, pipeline_ms_ = 0, fallback_ms_ = 0;
    std::chrono::steady_clock::time_point previous_ = std::chrono::steady_clock::now();
    std::optional<double> previous_presentation_seconds_;
};
} // namespace cubey::projects::raytracing
