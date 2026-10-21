#pragma once

#include "sdf_recipe.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <array>
#include <memory>
#include <stdexcept>

namespace cubey::projects::sdf {
class CapacityExceeded : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct FieldStats {
    std::uint32_t brick_count = 0;
    std::uint32_t reference_count = 0;
    std::uint32_t batch_count = 0;
    std::uint64_t allocation_bytes = 0;
    double build_ms = 0;
    double filter_gpu_ms = 0, brick_gpu_ms = 0, other_wall_ms = 0;
    std::uint64_t copied_bricks = 0, updated_bricks = 0;
};

// Project-local, deliberately bounded reference implementation of ProjectR's
// paired fields. A successful final brick dispatch is the commit boundary.
class Field {
  public:
    Field(vulkan::Device& device, ProjectGpuServices& gpu, std::uint32_t atlas_axis = 64,
          std::uint32_t batch_size = 128, std::uint32_t frontier_limit = 0,
          std::uint32_t reference_limit = 16U * 1024U * 1024U);
    void build(std::span<const BoxEdit> edits);
    [[nodiscard]] std::size_t displayed_edit_count() const {
        return displayed_edit_count_;
    }
    [[nodiscard]] const vulkan::Buffer& nodes() const {
        return *nodes_[displayed_];
    }
    [[nodiscard]] const render::Texture3D& atlas() const {
        return *atlases_[displayed_];
    }
    [[nodiscard]] const FieldStats& stats() const {
        return stats_;
    }
    static constexpr std::uint32_t depth = 8;
    const std::uint32_t atlas_axis;
    const std::uint32_t capacity;

  private:
    void dispatch(bool bricks, std::uint32_t level, std::uint32_t count, std::uint32_t input,
                  std::uint32_t output);
    void update_descriptors(std::uint32_t input, std::uint32_t output);
    vulkan::Device& device_;
    ProjectGpuServices& gpu_;
    const std::uint32_t batch_size_;
    const std::uint32_t reference_capacity_;
    std::array<std::unique_ptr<vulkan::Buffer>, 2> nodes_;
    std::array<std::unique_ptr<render::Texture3D>, 2> atlases_;
    std::array<std::unique_ptr<vulkan::Buffer>, 2> frontiers_;
    std::unique_ptr<vulkan::Buffer> edit_headers_, references_, edits_, counters_;
    std::unique_ptr<vulkan::DescriptorSetBundle> descriptors_;
    std::optional<render::ComputePipelineResource> filter_pipeline_, brick_pipeline_;
    std::optional<vulkan::GpuTimestampProfiler> profiler_;
    std::uint32_t committed_ = 0, displayed_ = 0, base_brick_count_ = 0;
    std::vector<GpuEdit> committed_edits_, last_requested_;
    bool has_last_requested_ = false;
    std::size_t displayed_edit_count_ = 0;
    FieldStats stats_;
};

} // namespace cubey::projects::sdf
