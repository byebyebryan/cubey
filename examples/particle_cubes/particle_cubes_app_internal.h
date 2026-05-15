#pragma once

#include "particle_cubes_app.h"

#include <cubey/core/math.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>

#ifndef CUBEY_PARTICLE_CUBES_SHADER_DIR
#error "CUBEY_PARTICLE_CUBES_SHADER_DIR must be defined by the particle_cubes CMake target"
#endif

namespace cubey::examples::particle_cubes {

inline constexpr std::uint32_t kParticleCubeCount = 4096;
inline constexpr std::uint32_t kComputeGroupSize = 128;
inline constexpr std::uint32_t kCubeTriangleCount = 12;
inline constexpr float kParticleCubeMinScale = 0.01F;
inline constexpr float kParticleCubeScaleRange = 0.03F;
inline constexpr float kCameraDistance = 6.4F;
inline constexpr float kCameraBasePitch = -0.32F;

struct ParticleCubeGpu {
    std::array<float, 4> position_scale{};
    std::array<float, 4> velocity_seed{};
    std::array<float, 4> color{};
};

struct DrawPushConstants {
    cubey::math::Mat4 view_projection;
};

struct ComputePushConstants {
    std::array<float, 4> attractor_dt{};
    std::array<float, 4> bounds_damping_time{};
};

static_assert(sizeof(ParticleCubeGpu) == 48);
static_assert(sizeof(DrawPushConstants) == sizeof(cubey::math::Mat4));
static_assert(sizeof(ComputePushConstants) == 32);

class ParticleCubesApp {
  public:
    explicit ParticleCubesApp(RunConfig config);

    ParticleCubesApp(const ParticleCubesApp&) = delete;
    ParticleCubesApp& operator=(const ParticleCubesApp&) = delete;

    int run();

  private:
    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context);
    void create_cube_mesh(cubey::host::WindowedAppContext& context);
    void create_particle_buffer(cubey::host::WindowedAppContext& context);
    void create_descriptor_resources(cubey::host::WindowedAppContext& context);
    void update_particle_descriptor(cubey::host::WindowedAppContext& context);
    void reset_particle_buffer(cubey::host::WindowedAppContext& context);
    void create_compute_resources(cubey::host::WindowedAppContext& context);
    void create_forward_pass(cubey::host::WindowedAppContext& context);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    [[nodiscard]] const cubey::render::Mesh& cube_mesh() const;
    [[nodiscard]] const cubey::vulkan::DescriptorSetBundle& descriptors() const;
    [[nodiscard]] const cubey::vulkan::Buffer& particle_buffer() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& compute_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const;

    void record_particle_compute(const cubey::vulkan::CommandRecorder& recorder,
                                 const FrameTiming& timing) const;
    [[nodiscard]] DrawPushConstants draw_push_constants(const FrameTiming& timing,
                                                        VkExtent2D extent) const;
    void record_particle_cubes_frame(const cubey::host::WindowedRenderFrame& frame);

    RunConfig config_;
    OrbitController orbit_controller_;
    bool paused_ = false;
    bool reset_cubes_requested_ = false;

    std::optional<cubey::render::Mesh> cube_mesh_;
    std::optional<cubey::vulkan::Buffer> particle_buffer_;
    std::optional<cubey::vulkan::DescriptorSetBundle> descriptors_;
    std::optional<cubey::render::ComputePipelineResource> compute_pipeline_resource_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace cubey::examples::particle_cubes
