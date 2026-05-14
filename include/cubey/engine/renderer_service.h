#pragma once

#include <cubey/engine/forward_pbr_renderer_3d.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace cubey {

class RendererService {
  public:
    RendererService() = default;
    ~RendererService();

    RendererService(const RendererService&) = delete;
    RendererService& operator=(const RendererService&) = delete;
    RendererService(RendererService&&) = delete;
    RendererService& operator=(RendererService&&) = delete;

    [[nodiscard]] ForwardPbrRenderer3D&
    create_forward_pbr_renderer_3d(ForwardPbrRenderer3DConfig config);
    void destroy_forward_pbr_renderer_3d(ForwardPbrRenderer3D& renderer);

    void destroy_swapchain_resources();
    void destroy_all_resources();

    [[nodiscard]] std::size_t renderer_count() const noexcept;

  private:
    std::vector<std::unique_ptr<ForwardPbrRenderer3D>> forward_pbr_renderers_;
};

} // namespace cubey
