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
    math::Vec4 glass{}, attenuation{};
};
static_assert(sizeof(EnvironmentUniform) == 240);
class Environment {
  public:
    Environment(vulkan::Device&, vulkan::GpuRuntime&, const Config&, std::uint32_t slots);
    ~Environment();
    EnvironmentUniform prepare(const Config&, VkCommandBuffer, render::FrameSlot, math::Vec3 eye,
                               math::Vec3& sun);
    void bind(const vulkan::Device&, VkDescriptorSet) const;
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
    double setup_ms_ = 0;
    double probe_gpu_ms_ = 0, pipeline_ms_ = 0, fallback_ms_ = 0;
    std::chrono::steady_clock::time_point previous_ = std::chrono::steady_clock::now();
};
} // namespace cubey::projects::raytracing
