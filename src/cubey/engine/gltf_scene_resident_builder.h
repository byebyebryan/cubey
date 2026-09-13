#pragma once

#include <cubey/engine/gltf_scene_importer.h>

#include <memory>
#include <optional>

namespace cubey {

// Private owner-thread half of a glTF upload session. It owns the prepared
// product while it builds the resident resources, but deliberately knows
// nothing about jobs, runtimes, or abandonment.
class GltfSceneResidentBuilder {
  public:
    struct AdvanceResult {
        bool made_progress = false;
        bool backpressured = false;
        bool finished_recording = false;
        std::optional<vulkan::GpuUploadStepTicket> submitted_ticket{};
    };

    GltfSceneResidentBuilder(std::shared_ptr<const GltfPreparedScene> prepared,
                             GltfSceneImportConfig config);
    ~GltfSceneResidentBuilder();

    GltfSceneResidentBuilder(const GltfSceneResidentBuilder&) = delete;
    GltfSceneResidentBuilder& operator=(const GltfSceneResidentBuilder&) = delete;

    [[nodiscard]] AdvanceResult advance(vulkan::GpuOwnerContext& owner);
    void mark_complete();
    [[nodiscard]] GltfSceneUploadSessionMetrics metrics() const;
    [[nodiscard]] GltfSceneResident take_resident();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cubey
