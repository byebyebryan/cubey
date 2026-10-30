#pragma once
#include "sdf_cached_edit.h"
#include "sdf_field.h"
#include "sdf_regions.h"

namespace cubey::projects::sdf {
enum class CommitBuilder {
    Full,
    Reuse,
    Pruned
};
struct CommitBuildStats {
    double admission_ms = 0, pipeline_prepare_ms = 0, region_prepare_ms = 0, wall_ms = 0;
    std::uint64_t region_bytes = 0, region_scratch_bytes = 0;
    std::size_t region_references = 0;
    double reuse_gpu_ms = 0, fallback_gpu_ms = 0;
    bool full_fallback = false;
    // Old/new bricks, reused texels, numerical fallback texels, new canonical
    // texels, unused, unused, list overflow.
    std::array<std::uint32_t, 8> work{};
};
struct CommitTopologyEntry {
    std::uint32_t key, flags;
    bool operator==(const CommitTopologyEntry&) const = default;
};
struct CommitSnapshot {
    std::vector<CommitTopologyEntry> topology;
    std::vector<std::uint32_t> cells;
    std::vector<std::uint16_t> samples;
};
// Private study only. The canonical Field entrypoints and their pipelines are
// unchanged. Source and destination are distinct; the old field stays immutable.
class CachedCommitStudy {
  public:
    CachedCommitStudy(vulkan::Device& device, ProjectGpuServices& gpu,
                      std::uint32_t fallback_limit = 2U * 1024U * 1024U);
    double prepare(CommitBuilder mode);
    [[nodiscard]] std::uint64_t working_bytes() const {
        return 132U + fallbacks_->size() + new_bricks_->size();
    }
    CommitBuildStats build(Field& destination, const Field& base, const CachedEditProgram& program,
                           CommitBuilder mode, std::uint32_t region_axis = 4);
    static CommitSnapshot snapshot(vulkan::Device& device, ProjectGpuServices& gpu,
                                   const Field& field);

  private:
    vulkan::Device& device_;
    ProjectGpuServices& gpu_;
    std::unique_ptr<vulkan::DescriptorSetBundle> descriptors_;
    std::unique_ptr<vulkan::Buffer> controls_, counts_, empty_cells_, empty_refs_;
    std::unique_ptr<vulkan::Buffer> fallbacks_, new_bricks_;
    std::uint32_t fallback_limit_;
    std::optional<render::ComputePipelineResource> reuse_, fallback_, pruned_bricks_,
        pruned_filter_;
    std::optional<vulkan::GpuTimestampProfiler> profiler_;
};
} // namespace cubey::projects::sdf
