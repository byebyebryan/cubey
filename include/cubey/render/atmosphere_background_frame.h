#pragma once

#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/atmosphere_night_sky_atlas.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/lunar_surface_map.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace cubey::render {

enum class AtmosphereBackgroundBinding : std::uint32_t {
    FrameUniforms = 0,
    NightSkyAtlas = 1,
};

struct AtmosphereBackgroundTextureBindings {
    VkSampler lunar_surface_sampler = VK_NULL_HANDLE;
    VkImageView lunar_surface_view = VK_NULL_HANDLE;
    VkImageLayout lunar_surface_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkSampler night_sky_sampler = VK_NULL_HANDLE;
    VkImageView night_sky_view = VK_NULL_HANDLE;
    VkImageLayout night_sky_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct AtmosphereBackgroundAtlasResources {
    Texture2D lunar_surface;
    TextureCube night_sky;
    bool lunar_surface_placeholder = false;
    bool night_sky_placeholder = false;

    [[nodiscard]] AtmosphereBackgroundTextureBindings bindings() const;
};

using AtmosphereBackgroundPlaceholderTextures = AtmosphereBackgroundAtlasResources;

struct AtmosphereBackgroundGeneratedAtlasConfig {
    NightSkyAtlasConfig night_sky{};
    std::uint32_t lunar_surface_width = kLunarSurfaceMapWidth;
    std::uint32_t lunar_surface_height = kLunarSurfaceMapHeight;
    std::uint32_t night_sky_extent = kNightSkyAtlasExtent;
};

[[nodiscard]] Texture2D create_lunar_surface_map_texture(const cubey::vulkan::Device& device,
                                                         cubey::vulkan::GpuRuntime& gpu,
                                                         const LunarSurfaceMap& map);
[[nodiscard]] Texture2D create_lunar_surface_map_texture(const cubey::vulkan::Device& device,
                                                         cubey::vulkan::GpuOwnerContext& context,
                                                         const LunarSurfaceMap& map);
[[nodiscard]] TextureCube
create_atmosphere_night_sky_atlas_texture(const cubey::vulkan::Device& device,
                                          cubey::vulkan::GpuRuntime& gpu,
                                          const NightSkyAtlas& atlas);
[[nodiscard]] TextureCube
create_atmosphere_night_sky_atlas_texture(const cubey::vulkan::Device& device,
                                          cubey::vulkan::GpuOwnerContext& context,
                                          const NightSkyAtlas& atlas);

[[nodiscard]] AtmosphereBackgroundAtlasResources create_atmosphere_background_atlas_resources(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const LunarSurfaceMap& lunar_surface, const NightSkyAtlas& night_sky);
[[nodiscard]] AtmosphereBackgroundAtlasResources create_atmosphere_background_generated_textures(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const AtmosphereBackgroundGeneratedAtlasConfig& config = {});
[[nodiscard]] AtmosphereBackgroundAtlasResources
create_atmosphere_background_placeholder_textures(const cubey::vulkan::Device& device,
                                                  cubey::vulkan::GpuRuntime& gpu);

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

using AtmosphereBackgroundFrameMaterial =
    FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>;

[[nodiscard]] MaterialPassInfo atmosphere_background_pass_info();
void validate_atmosphere_background_texture_bindings(
    const AtmosphereBackgroundTextureBindings& textures);
void update_atmosphere_background_frame_material_texture_bindings(
    const cubey::vulkan::Device& device, const AtmosphereBackgroundFrameMaterial& material,
    const AtmosphereBackgroundTextureBindings& textures);
[[nodiscard]] std::shared_ptr<AtmosphereBackgroundFrameMaterial>
create_atmosphere_background_frame_material(const cubey::vulkan::Device& device,
                                            const AtmosphereBackgroundFrameMaterialConfig& config);

class AtmosphereBackgroundFrame {
  public:
    AtmosphereBackgroundFrame() = default;

    AtmosphereBackgroundFrame(const AtmosphereBackgroundFrame&) = delete;
    AtmosphereBackgroundFrame& operator=(const AtmosphereBackgroundFrame&) = delete;
    AtmosphereBackgroundFrame(AtmosphereBackgroundFrame&&) = delete;
    AtmosphereBackgroundFrame& operator=(AtmosphereBackgroundFrame&&) = delete;

    void create_materials(const cubey::vulkan::Device& device,
                          const AtmosphereBackgroundFrameMaterialConfig& config);
    void install_materials(std::shared_ptr<AtmosphereBackgroundFrameMaterial> material);
    void update_texture_bindings(const cubey::vulkan::Device& device,
                                 const AtmosphereBackgroundTextureBindings& textures) const;
    void create_pipeline(const cubey::vulkan::Device& device,
                         const AtmosphereBackgroundFramePipelineConfig& config);
    void destroy_pipeline();
    void destroy();

    void upload(FrameSlot frame_slot, const AtmosphereEnvironmentFrameUniforms& uniforms) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder, ColorTargetView target,
                     FrameSlot frame_slot) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder, ColorTargetView target,
                     VkDescriptorSet descriptor_set) const;

    [[nodiscard]] bool materials_created() const noexcept;
    [[nodiscard]] const AtmosphereBackgroundFrameMaterial& material() const;
    [[nodiscard]] const GraphicsPipelineResource& pipeline() const;

  private:
    std::shared_ptr<AtmosphereBackgroundFrameMaterial> material_{};
    std::optional<GraphicsPipelineResource> pipeline_;
};

} // namespace cubey::render
