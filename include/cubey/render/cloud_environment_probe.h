#pragma once

#include <cubey/render/atmosphere_reflection_probe.h>
#include <cubey/render/cloud_layer.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace cubey::render {

struct CloudEnvironmentProbeConfig {
    std::uint32_t extent = 64;
    std::uint32_t mip_levels = 5;
    std::uint32_t view_steps = 32;
    float update_hz = 4.0F;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::uint32_t frame_slot_count = 1;
};

struct CloudEnvironmentProbePipelineConfig {
    ShaderStageFile cloud_march{};
    ShaderStageFile prefilter_vertex{};
    ShaderStageFile prefilter_fragment{};
};

struct CloudEnvironmentProbeUpdateInfo {
    FrameSlot frame_slot{};
    CloudLayerConfig cloud{};
    CloudLayerFrameInfo frame{};
};

struct CloudEnvironmentProbeSnapshot {
    const TextureCube* previous = nullptr;
    const TextureCube* current = nullptr;
    float blend = 1.0F;
    std::uint64_t generation = 0;
    bool valid = false;
};

class CloudEnvironmentProbeTimeline {
  public:
    void configure(float update_hz);
    void reset();
    void advance(double delta_seconds);
    void capture_recorded();

    [[nodiscard]] bool capture_pending() const noexcept {
        return capture_pending_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }
    [[nodiscard]] float blend() const noexcept {
        return blend_;
    }
    [[nodiscard]] float age_seconds() const noexcept {
        return age_seconds_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }

  private:
    float update_interval_seconds_ = 0.25F;
    float age_seconds_ = 0.0F;
    float blend_ = 1.0F;
    std::uint64_t generation_ = 0;
    bool valid_ = false;
    bool capture_pending_ = true;
};

[[nodiscard]] MaterialPassInfo cloud_environment_prefilter_pass_info();
void validate_cloud_environment_probe_config(const CloudEnvironmentProbeConfig& config);

class CloudEnvironmentProbe {
  public:
    CloudEnvironmentProbe() = default;

    CloudEnvironmentProbe(const CloudEnvironmentProbe&) = delete;
    CloudEnvironmentProbe& operator=(const CloudEnvironmentProbe&) = delete;
    CloudEnvironmentProbe(CloudEnvironmentProbe&&) = delete;
    CloudEnvironmentProbe& operator=(CloudEnvironmentProbe&&) = delete;

    void create_resources(const cubey::vulkan::Device& device,
                          const CloudEnvironmentProbeConfig& config,
                          const CloudLayerGeneratedResources& generated,
                          const TextureCube& clear_sky);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const CloudEnvironmentProbePipelineConfig& config);
    void destroy();

    void advance(double delta_seconds);
    void invalidate();
    [[nodiscard]] bool record_pending_update(const cubey::vulkan::CommandRecorder& recorder,
                                             const CloudEnvironmentProbeUpdateInfo& info);

    [[nodiscard]] bool resources_created() const noexcept;
    [[nodiscard]] bool pipelines_created() const noexcept;
    [[nodiscard]] bool capture_pending() const noexcept {
        return timeline_.capture_pending();
    }
    [[nodiscard]] float age_seconds() const noexcept {
        return timeline_.age_seconds();
    }
    [[nodiscard]] CloudEnvironmentProbeSnapshot snapshot() const;

  private:
    struct ProbeBuffer {
        std::optional<TextureCube> contribution{};
        std::optional<TextureCube> metadata{};
        std::optional<TextureCube> prefiltered{};
        std::vector<cubey::vulkan::ImageView> contribution_faces{};
        std::vector<cubey::vulkan::ImageView> metadata_faces{};
        std::vector<cubey::vulkan::ImageView> prefiltered_faces{};
        std::array<std::unique_ptr<FrameUniformMaterialInstance<CloudLayerFrameUniforms>>, 6>
            march_materials{};
        std::vector<std::unique_ptr<
            FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>>>
            prefilter_materials{};
        std::array<bool, 6> contribution_initialized{};
        std::array<bool, 6> metadata_initialized{};
        std::vector<bool> prefiltered_initialized{};
    };

    void create_buffer_resources(const cubey::vulkan::Device& device, ProbeBuffer& buffer,
                                 const CloudLayerGeneratedResources& generated,
                                 const TextureCube& clear_sky);
    void record_capture(const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot,
                        const CloudLayerConfig& cloud, const CloudLayerFrameInfo& frame,
                        std::uint32_t buffer_index);
    void record_cloud_faces(const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot,
                            const CloudLayerConfig& cloud, const CloudLayerFrameInfo& frame,
                            ProbeBuffer& buffer);
    void record_prefilter_faces(const cubey::vulkan::CommandRecorder& recorder,
                                FrameSlot frame_slot, ProbeBuffer& buffer);
    void transition_face(const cubey::vulkan::CommandRecorder& recorder, VkImage image,
                         std::uint32_t mip_level, std::uint32_t face_index,
                         VkImageLayout old_layout, VkImageLayout new_layout,
                         VkAccessFlags src_access, VkAccessFlags dst_access,
                         VkPipelineStageFlags src_stage,
                         VkPipelineStageFlags dst_stage) const;
    [[nodiscard]] FrameUniformMaterialInstance<CloudLayerFrameUniforms>&
    march_material(ProbeBuffer& buffer, std::uint32_t face_index) const;
    [[nodiscard]] FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>&
    prefilter_material(ProbeBuffer& buffer, std::uint32_t mip_level,
                       std::uint32_t face_index) const;

    CloudEnvironmentProbeConfig config_{};
    std::array<ProbeBuffer, 2> buffers_{};
    std::optional<ComputePipelineResource> march_pipeline_{};
    std::optional<GraphicsPipelineResource> prefilter_pipeline_{};
    CloudEnvironmentProbeTimeline timeline_{};
    std::uint32_t previous_buffer_index_ = 0;
    std::uint32_t current_buffer_index_ = 0;
};

} // namespace cubey::render
