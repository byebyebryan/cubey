#pragma once
#include "sdf_cached_edit.h"
#include "sdf_cached_stroke.h"
#include "sdf_field.h"
#include "sdf_regions.h"
#include <functional>
#include <unordered_map>

namespace cubey::projects::sdf {
enum class CommitBuilder {
    Full,
    Reuse,
    Pruned,
    Local,
    LocalDominance
};
struct CommitCopyStats {
    double wall_ms = 0, gpu_ms = 0, max_chunk_gpu_ms = 0;
    std::uint64_t bytes = 0;
    std::uint32_t chunks = 0;
};
struct CommitUnit {
    std::string kind;
    unsigned level = 0, groups = 0;
    double gpu_ms = 0, wall_ms = 0;
};
struct CommitSlicing {
    unsigned groups = 0, copy_slab_bytes = 0;
    std::function<void(std::string_view)> before;
    std::function<void(const CommitUnit&)> after;
};
struct CommitBuildStats {
    double admission_ms = 0, pipeline_prepare_ms = 0, region_prepare_ms = 0, wall_ms = 0;
    double palette_ms = 0, validation_ms = 0;
    AuthoredSerializationStats serialization;
    double palette_reparse_ms = 0;
    std::uint64_t region_bytes = 0, region_scratch_bytes = 0;
    std::size_t region_references = 0;
    double reuse_gpu_ms = 0, fallback_gpu_ms = 0;
    bool full_fallback = false;
    CommitCopyStats copy;
    float locality_bound = 0, locality_guard = 0;
    std::array<std::uint32_t, 16> local_work{};
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
    std::vector<std::uint32_t> slots;
};
// Private study only. The canonical Field entrypoints and their pipelines are
// unchanged. Source and destination are distinct; the old field stays immutable.
class CachedCommitStudy {
  public:
    CachedCommitStudy(vulkan::Device& device, ProjectGpuServices& gpu,
                      std::uint32_t fallback_limit = 2U * 1024U * 1024U);
    double prepare(CommitBuilder mode);
    // Private cooperative scheduling. Callbacks run on the caller/worker, never
    // while holding the GPU owner. They may defer the next bounded submission.
    void set_slicing(CommitSlicing slicing) {
        slicing_ = std::move(slicing);
    }
    [[nodiscard]] std::uint64_t working_bytes() const {
        return 132U + fallbacks_->size() + new_bricks_->size() +
               (local_entries_ ? local_entries_->size() + 608U : 0U);
    }
    CommitBuildStats build(Field& destination, const Field& base, const CachedEditProgram& program,
                           CommitBuilder mode, std::uint32_t region_axis = 4);
    CommitBuildStats build(Field& destination, const Field& base,
                           const CachedStrokeProgram& program, CommitBuilder mode,
                           std::uint32_t region_axis = 4,
                           StrokePreparation preparation = StrokePreparation::Legacy);
    CommitBuildStats build(Field& destination, const Field& base,
                           const PreparedCachedStroke& program, const StrokeVersion& current,
                           CommitBuilder mode);
    static CommitSnapshot snapshot(vulkan::Device& device, ProjectGpuServices& gpu,
                                   const Field& field);
    CommitCopyStats clone(Field& destination, const Field& base, std::uint32_t slab_bytes = 0,
                          bool publish = false);

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
    CommitBuildStats build_local(Field& destination, const Field& base,
                                 const AuthoredScene& base_scene, const AuthoredScene& edited_scene,
                                 const CachedEditControls& edit_controls,
                                 std::span<const SceneInstruction> tools = {},
                                 bool dominance = false);
    std::unique_ptr<vulkan::Buffer> local_controls_, local_counts_, local_entries_;
    std::optional<render::ComputePipelineResource> local_filter_, local_samples_, local_canonical_;
    std::optional<render::ComputePipelineResource> local_dom_filter_, local_dom_samples_;
    struct LocalFieldState {
        std::uint64_t hash;
        std::uint32_t high_water;
    };
    std::unordered_map<const Field*, LocalFieldState> local_fields_;
    CommitSlicing slicing_;
    std::array<std::optional<vulkan::ComputePipeline>, 5> sliced_pipelines_;
};
} // namespace cubey::projects::sdf
