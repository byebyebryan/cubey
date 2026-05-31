#pragma once

#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>

namespace cubey::render {

enum class AtmosphereBackgroundBinding : std::uint32_t {
    FrameUniforms = 0,
    MoonAtlas = 1,
    NightSkyAtlas = 2,
};

struct AtmosphereBackgroundTextureBindings {
    VkSampler lunar_sampler = VK_NULL_HANDLE;
    VkImageView lunar_view = VK_NULL_HANDLE;
    VkImageLayout lunar_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkSampler night_sky_sampler = VK_NULL_HANDLE;
    VkImageView night_sky_view = VK_NULL_HANDLE;
    VkImageLayout night_sky_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct AtmosphereBackgroundFrameMaterialConfig {
    std::uint32_t frame_slot_count = 1;
    AtmosphereBackgroundTextureBindings textures{};
};

struct AtmosphereBackgroundFramePipelineConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    std::span<const ShaderStageFile> shader_stage_files{};
};

[[nodiscard]] MaterialPassInfo atmosphere_background_pass_info();

class AtmosphereBackgroundFrame {
  public:
    AtmosphereBackgroundFrame() = default;

    AtmosphereBackgroundFrame(const AtmosphereBackgroundFrame&) = delete;
    AtmosphereBackgroundFrame& operator=(const AtmosphereBackgroundFrame&) = delete;
    AtmosphereBackgroundFrame(AtmosphereBackgroundFrame&&) = delete;
    AtmosphereBackgroundFrame& operator=(AtmosphereBackgroundFrame&&) = delete;

    void create_materials(const cubey::vulkan::Device& device,
                          const AtmosphereBackgroundFrameMaterialConfig& config);
    void update_texture_bindings(const cubey::vulkan::Device& device,
                                 const AtmosphereBackgroundTextureBindings& textures) const;
    void create_pipeline(const cubey::vulkan::Device& device,
                         const AtmosphereBackgroundFramePipelineConfig& config);
    void destroy_pipeline();
    void destroy();

    void upload(FrameSlot frame_slot, const AtmosphereEnvironmentFrameUniforms& uniforms) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder, ColorTargetView target,
                     FrameSlot frame_slot) const;

    [[nodiscard]] bool materials_created() const noexcept;
    [[nodiscard]] const FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>&
    material() const;
    [[nodiscard]] const GraphicsPipelineResource& pipeline() const;

  private:
    std::optional<FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>> material_;
    std::optional<GraphicsPipelineResource> pipeline_;
};

} // namespace cubey::render
