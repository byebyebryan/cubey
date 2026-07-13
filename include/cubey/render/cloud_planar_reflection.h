#pragma once

#include <cubey/render/cloud_layer.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace cubey::render {

struct CloudPlanarReflectionConfig {
    VkExtent2D target_extent{1U, 1U};
    float resolution_scale = 0.5F;
    std::uint32_t mip_levels = 6U;
    std::uint32_t view_steps = 32U;
    float guard_band = 0.15F;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::uint32_t frame_slot_count = 1U;
};

struct CloudPlanarReflectionPipelineConfig {
    ShaderStageFile cloud_march{};
    ShaderStageFile filter_vertex{};
    ShaderStageFile filter_fragment{};
};

struct CloudPlanarReflectionRequest {
    FrameSlot frame_slot{};
    CloudLayerConfig cloud{};
    CloudLayerFrameInfo frame{};
    math::Vec3 plane_point{0.0F, 0.0F, 0.0F};
    math::Vec3 plane_normal{0.0F, 1.0F, 0.0F};
};

struct CloudPlanarReflectionSnapshot {
    const Texture2D* texture = nullptr;
    ViewRayBasis3D view_rays{};
    float max_lod = 0.0F;
    bool valid = false;
};

[[nodiscard]] MaterialPassInfo cloud_planar_reflection_filter_pass_info();
void validate_cloud_planar_reflection_config(const CloudPlanarReflectionConfig& config);
[[nodiscard]] VkExtent2D
cloud_planar_reflection_extent(const CloudPlanarReflectionConfig& config);
[[nodiscard]] CloudLayerFrameInfo
cloud_planar_reflected_frame(const CloudLayerFrameInfo& frame, math::Vec3 plane_point,
                             math::Vec3 plane_normal, VkExtent2D target_extent,
                             float guard_band);

class CloudPlanarReflectionRuntime {
  public:
    CloudPlanarReflectionRuntime() = default;

    CloudPlanarReflectionRuntime(const CloudPlanarReflectionRuntime&) = delete;
    CloudPlanarReflectionRuntime& operator=(const CloudPlanarReflectionRuntime&) = delete;
    CloudPlanarReflectionRuntime(CloudPlanarReflectionRuntime&&) = delete;
    CloudPlanarReflectionRuntime& operator=(CloudPlanarReflectionRuntime&&) = delete;

    void create_resources(const cubey::vulkan::Device& device,
                          const CloudPlanarReflectionConfig& config,
                          const CloudLayerGeneratedResources& generated);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const CloudPlanarReflectionPipelineConfig& config);
    void destroy();

    void record(const cubey::vulkan::CommandRecorder& recorder,
                const CloudPlanarReflectionRequest& request);
    [[nodiscard]] CloudPlanarReflectionSnapshot snapshot(FrameSlot frame_slot) const;
    [[nodiscard]] bool resources_created() const noexcept;
    [[nodiscard]] bool pipelines_created() const noexcept;

  private:
    struct Buffer {
        std::optional<Texture2D> contribution{};
        std::optional<Texture2D> metadata{};
        std::optional<Texture2D> filtered{};
        std::vector<cubey::vulkan::ImageView> filtered_mips{};
        std::vector<bool> filtered_initialized{};
        ViewRayBasis3D view_rays{};
        bool contribution_initialized = false;
        bool metadata_initialized = false;
        bool valid = false;
    };

    void transition_mip(const cubey::vulkan::CommandRecorder& recorder, VkImage image,
                        std::uint32_t mip_level, VkImageLayout old_layout,
                        VkImageLayout new_layout, VkAccessFlags src_access,
                        VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                        VkPipelineStageFlags dst_stage) const;
    [[nodiscard]] Buffer& buffer(FrameSlot frame_slot);
    [[nodiscard]] const Buffer& buffer(FrameSlot frame_slot) const;
    [[nodiscard]] MaterialInstance& filter_material(std::uint32_t mip_level) const;

    CloudPlanarReflectionConfig config_{};
    std::vector<Buffer> buffers_{};
    std::unique_ptr<FrameUniformMaterialInstance<CloudLayerFrameUniforms>> march_material_{};
    std::vector<std::unique_ptr<MaterialInstance>> filter_materials_{};
    std::optional<ComputePipelineResource> march_pipeline_{};
    std::optional<GraphicsPipelineResource> filter_pipeline_{};
};

} // namespace cubey::render
