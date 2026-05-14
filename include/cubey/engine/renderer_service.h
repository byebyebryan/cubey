#pragma once

#include <cubey/engine/pbr_view_renderer.h>

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

    [[nodiscard]] PbrViewRenderer3D&
    create_pbr_view_renderer_3d(PbrViewRenderer3DConfig config);
    void destroy_pbr_view_renderer_3d(PbrViewRenderer3D& renderer);

    void destroy_swapchain_resources();
    void destroy_all_resources();

    [[nodiscard]] std::size_t renderer_count() const noexcept;

  private:
    std::vector<std::unique_ptr<PbrViewRenderer3D>> pbr_view_renderers_;
};

} // namespace cubey
