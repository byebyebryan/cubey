#pragma once

#include "headless_cube_app.h"

#include <cubey/core/math.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/mesh.h>

#include <vulkan/vulkan.h>

#include <optional>

#ifndef CUBEY_HEADLESS_CUBE_SHADER_DIR
#error "CUBEY_HEADLESS_CUBE_SHADER_DIR must be defined by the headless_cube CMake target"
#endif

namespace cubey::examples::headless_cube {

class HeadlessCubeApp {
  public:
    explicit HeadlessCubeApp(RunConfig config);

    HeadlessCubeApp(const HeadlessCubeApp&) = delete;
    HeadlessCubeApp& operator=(const HeadlessCubeApp&) = delete;

    int run();

  private:
    void create_global_resources_if_needed(cubey::host::HeadlessPngContext& context);
    void create_forward_pass(cubey::vulkan::Device& device,
                             const cubey::host::HeadlessRenderTarget& target);
    void destroy_resources();

    [[nodiscard]] cubey::math::Mat4 cube_mvp(VkExtent2D extent) const;
    void render_png(VkCommandBuffer command_buffer,
                    const cubey::host::HeadlessRenderTarget& target) const;
    [[nodiscard]] const cubey::render::Mesh& cube_mesh() const;
    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const;

    RunConfig config_;
    std::optional<cubey::render::Mesh> cube_mesh_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace cubey::examples::headless_cube
