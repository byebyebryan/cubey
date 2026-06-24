#include <cubey/render/atmosphere_reflection_probe.h>

#include <cubey/render/pass.h>
#include <cubey/vulkan/image_transitions.h>

#include <array>
#include <memory>
#include <stdexcept>

namespace cubey::render {
namespace {

[[nodiscard]] constexpr std::uint32_t binding(AtmosphereReflectionPrefilterBinding value) noexcept {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::size_t face_mip_index(std::uint32_t mip_level, std::uint32_t face_index) {
    return static_cast<std::size_t>(mip_level) * 6U + static_cast<std::size_t>(face_index);
}

[[nodiscard]] ViewRayBasis3D cube_face_basis(math::Vec3 right, math::Vec3 up, math::Vec3 forward) {
    return {
        .right_aspect = {right.x, right.y, right.z, 1.0F},
        .up_tan_half_fovy = {up.x, up.y, up.z, 1.0F},
        .forward = {forward.x, forward.y, forward.z, 0.0F},
    };
}

[[nodiscard]] AtmosphereReflectionPrefilterUniforms
prefilter_uniforms(const ViewRayBasis3D& view_rays, float roughness, std::uint32_t mip_level) {
    return {
        .right_roughness =
            {
                view_rays.right_aspect.x,
                view_rays.right_aspect.y,
                view_rays.right_aspect.z,
                roughness,
            },
        .up_sample_count =
            {
                view_rays.up_tan_half_fovy.x,
                view_rays.up_tan_half_fovy.y,
                view_rays.up_tan_half_fovy.z,
                roughness <= 0.0001F ? 1.0F : 96.0F,
            },
        .forward_mip_level =
            {
                view_rays.forward.x,
                view_rays.forward.y,
                view_rays.forward.z,
                static_cast<float>(mip_level),
            },
    };
}

[[nodiscard]] FrameUniformMaterialInstanceConfig
atmosphere_background_material_config(std::uint32_t frame_slot_count,
                                      const AtmosphereBackgroundTextureBindings& textures) {
    return FrameUniformMaterialInstanceConfig{
        .material_pass = atmosphere_background_pass_info(),
        .descriptor_set = 0,
        .frame_slot_count = frame_slot_count,
        .uniform_binding = static_cast<std::uint32_t>(AtmosphereBackgroundBinding::FrameUniforms),
        .sampled_images =
            {
                SampledImageMaterialBinding{
                    .binding =
                        static_cast<std::uint32_t>(AtmosphereBackgroundBinding::NightSkyAtlas),
                    .sampler = textures.night_sky_sampler,
                    .image_view = textures.night_sky_view,
                    .layout = textures.night_sky_layout,
                },
            },
    };
}

[[nodiscard]] FrameUniformMaterialInstanceConfig
prefilter_material_config(std::uint32_t frame_slot_count, const TextureCube& sky_radiance_cube) {
    return FrameUniformMaterialInstanceConfig{
        .material_pass = atmosphere_reflection_prefilter_pass_info(),
        .descriptor_set = 0,
        .frame_slot_count = frame_slot_count,
        .uniform_binding = binding(AtmosphereReflectionPrefilterBinding::FrameUniforms),
        .sampled_images =
            {
                SampledImageMaterialBinding{
                    .binding = binding(AtmosphereReflectionPrefilterBinding::SkyRadianceCube),
                    .sampler = sky_radiance_cube.sampler().handle(),
                    .image_view = sky_radiance_cube.view(),
                },
            },
    };
}

} // namespace

MaterialPassInfo atmosphere_reflection_prefilter_pass_info() {
    return MaterialPassInfo{
        .label = "atmosphere.reflection.prefilter",
        .descriptor_sets =
            {
                MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    binding(AtmosphereReflectionPrefilterBinding::FrameUniforms),
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    binding(AtmosphereReflectionPrefilterBinding::SkyRadianceCube),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
    };
}

std::array<ViewRayBasis3D, 6> atmosphere_reflection_probe_cube_face_view_rays() {
    return {
        cube_face_basis({0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
        cube_face_basis({0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}),
        cube_face_basis({1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}),
        cube_face_basis({1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, -1.0F, 0.0F}),
        cube_face_basis({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
        cube_face_basis({-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}),
    };
}

void AtmosphereReflectionProbe::create_resources(const cubey::vulkan::Device& device,
                                                 const AtmosphereReflectionProbeConfig& config) {
    if (config.extent == 0 || config.mip_levels == 0) {
        throw std::runtime_error("atmosphere reflection probe dimensions must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("atmosphere reflection probe requires a color format");
    }
    if (config.frame_slot_count == 0) {
        throw std::runtime_error("atmosphere reflection probe requires at least one frame slot");
    }
    if (resources_created()) {
        throw std::runtime_error("atmosphere reflection probe resources are already initialized");
    }
    validate_atmosphere_background_texture_bindings(config.atmosphere_textures);

    extent_ = config.extent;
    mip_levels_ = config.mip_levels;
    format_ = config.format;
    const cubey::vulkan::SamplerConfig cube_sampler{
        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .max_lod = static_cast<float>(mip_levels_ - 1U),
    };
    sky_radiance_cube_.emplace(device, TextureCubeConfig{
                                           .extent = extent_,
                                           .mip_levels = 1,
                                           .format = format_,
                                           .usage = TextureCubeUsage::ColorAttachmentSampled,
                                           .create_sampler = true,
                                           .sampler = cube_sampler,
                                       });
    prefiltered_cube_.emplace(device, TextureCubeConfig{
                                          .extent = extent_,
                                          .mip_levels = mip_levels_,
                                          .format = format_,
                                          .usage = TextureCubeUsage::ColorAttachmentSampled,
                                          .create_sampler = true,
                                          .sampler = cube_sampler,
                                      });

    sky_face_views_.reserve(6U);
    for (std::uint32_t face = 0; face < 6U; ++face) {
        sky_face_views_.push_back(
            create_texture_cube_face_view(device, *sky_radiance_cube_, 0, face));
    }
    prefiltered_face_views_.reserve(static_cast<std::size_t>(mip_levels_) * 6U);
    for (std::uint32_t mip = 0; mip < mip_levels_; ++mip) {
        for (std::uint32_t face = 0; face < 6U; ++face) {
            prefiltered_face_views_.push_back(
                create_texture_cube_face_view(device, *prefiltered_cube_, mip, face));
        }
    }
    sky_face_initialized_.fill(false);
    prefiltered_face_mip_initialized_.assign(static_cast<std::size_t>(mip_levels_) * 6U, false);

    atmosphere_frame_.create_materials(device, AtmosphereBackgroundFrameMaterialConfig{
                                                   .frame_slot_count = config.frame_slot_count,
                                                   .textures = config.atmosphere_textures,
                                               });
    const FrameUniformMaterialInstanceConfig sky_material_config =
        atmosphere_background_material_config(config.frame_slot_count, config.atmosphere_textures);
    for (std::unique_ptr<FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>>&
             material : sky_face_materials_) {
        material =
            std::make_unique<FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>>(
                device, sky_material_config);
    }

    const FrameUniformMaterialInstanceConfig prefilter_config =
        prefilter_material_config(config.frame_slot_count, *sky_radiance_cube_);
    prefilter_materials_.reserve(static_cast<std::size_t>(mip_levels_) * 6U);
    for (std::uint32_t mip = 0; mip < mip_levels_; ++mip) {
        for (std::uint32_t face = 0; face < 6U; ++face) {
            prefilter_materials_.push_back(
                std::make_unique<
                    FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>>(
                    device, prefilter_config));
        }
    }
}

void AtmosphereReflectionProbe::create_pipelines(
    const cubey::vulkan::Device& device, const AtmosphereReflectionProbePipelineConfig& config) {
    if (!resources_created()) {
        throw std::runtime_error("atmosphere reflection probe resources are not initialized");
    }
    if (config.atmosphere_vertex_shader.empty() || config.atmosphere_fragment_shader.empty() ||
        config.prefilter_vertex_shader.empty() || config.prefilter_fragment_shader.empty()) {
        throw std::runtime_error("atmosphere reflection probe requires shader paths");
    }

    const std::array<ShaderStageFile, 2> atmosphere_shaders{
        vertex_shader_file(config.atmosphere_vertex_shader),
        fragment_shader_file(config.atmosphere_fragment_shader),
    };
    atmosphere_frame_.create_pipeline(device, AtmosphereBackgroundFramePipelineConfig{
                                                  .extent = {extent_, extent_},
                                                  .color_format = format_,
                                                  .shader_stage_files = atmosphere_shaders,
                                              });

    const std::array<ShaderStageFile, 2> prefilter_shaders{
        vertex_shader_file(config.prefilter_vertex_shader),
        fragment_shader_file(config.prefilter_fragment_shader),
    };
    const std::array<VkDescriptorSetLayout, 1> layouts{prefilter_material(0, 0).layout()};
    prefilter_pipeline_.emplace(
        device, graphics_pipeline_file_resource_config(
                    {
                        .extent = {extent_, extent_},
                        .color_format = format_,
                    },
                    {
                        .shader_stage_files = prefilter_shaders,
                        .descriptor_set_layouts = layouts,
                        .material_pass = atmosphere_reflection_prefilter_pass_info(),
                    }));
}

void AtmosphereReflectionProbe::destroy_pipelines() {
    prefilter_pipeline_.reset();
    atmosphere_frame_.destroy_pipeline();
}

void AtmosphereReflectionProbe::destroy() {
    destroy_pipelines();
    prefilter_materials_.clear();
    for (std::unique_ptr<FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>>&
             material : sky_face_materials_) {
        material.reset();
    }
    atmosphere_frame_.destroy();
    prefiltered_face_views_.clear();
    sky_face_views_.clear();
    prefiltered_cube_.reset();
    sky_radiance_cube_.reset();
    prefiltered_face_mip_initialized_.clear();
    sky_face_initialized_.fill(false);
    extent_ = 0;
    mip_levels_ = 0;
    format_ = VK_FORMAT_UNDEFINED;
}

void AtmosphereReflectionProbe::record_full_update(
    const cubey::vulkan::CommandRecorder& recorder,
    const AtmosphereReflectionProbeUpdateInfo& info) {
    for (std::uint32_t face = 0; face < 6U; ++face) {
        record_sky_face(recorder, info.frame_slot, info.environment, face);
    }
    for (std::uint32_t mip = 0; mip < mip_levels_; ++mip) {
        for (std::uint32_t face = 0; face < 6U; ++face) {
            record_prefilter_face_mip(recorder, info.frame_slot, face, mip);
        }
    }
}

void AtmosphereReflectionProbe::record_face_update(const cubey::vulkan::CommandRecorder& recorder,
                                                   const AtmosphereReflectionProbeUpdateInfo& info,
                                                   std::uint32_t face_index) {
    if (face_index >= 6U) {
        throw std::runtime_error("atmosphere reflection probe face index is out of range");
    }
    record_sky_face(recorder, info.frame_slot, info.environment, face_index);
    for (std::uint32_t mip = 0; mip < mip_levels_; ++mip) {
        record_prefilter_face_mip(recorder, info.frame_slot, face_index, mip);
    }
}

void AtmosphereReflectionProbe::update_atmosphere_texture_bindings(
    const cubey::vulkan::Device& device,
    const AtmosphereBackgroundTextureBindings& textures) const {
    atmosphere_frame_.update_texture_bindings(device, textures);
    for (const std::unique_ptr<FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>>&
             material : sky_face_materials_) {
        if (material == nullptr) {
            throw std::runtime_error(
                "atmosphere reflection probe sky face material is not initialized");
        }
        update_atmosphere_background_frame_material_texture_bindings(device, *material, textures);
    }
}

bool AtmosphereReflectionProbe::resources_created() const noexcept {
    return sky_radiance_cube_.has_value() && prefiltered_cube_.has_value();
}

bool AtmosphereReflectionProbe::pipelines_created() const noexcept {
    return prefilter_pipeline_.has_value();
}

const TextureCube& AtmosphereReflectionProbe::sky_radiance_cube() const {
    if (!sky_radiance_cube_.has_value()) {
        throw std::runtime_error("atmosphere reflection probe sky cube is not initialized");
    }
    return sky_radiance_cube_.value();
}

const TextureCube& AtmosphereReflectionProbe::prefiltered_cube() const {
    if (!prefiltered_cube_.has_value()) {
        throw std::runtime_error("atmosphere reflection probe prefiltered cube is not initialized");
    }
    return prefiltered_cube_.value();
}

ColorTargetView
AtmosphereReflectionProbe::face_target(const TextureCube& texture, std::uint32_t extent,
                                       VkFormat format,
                                       const std::vector<cubey::vulkan::ImageView>& views,
                                       std::uint32_t mip_level, std::uint32_t face_index) const {
    if (face_index >= 6U || mip_level >= texture.mip_levels()) {
        throw std::runtime_error("atmosphere reflection probe target subresource is out of range");
    }
    const std::uint32_t mip_extent = texture_cube_mip_extent(extent, mip_level);
    return color_target_view({mip_extent, mip_extent}, format, texture.handle(),
                             views.at(face_mip_index(mip_level, face_index)).handle());
}

void AtmosphereReflectionProbe::record_sky_face(const cubey::vulkan::CommandRecorder& recorder,
                                                FrameSlot frame_slot,
                                                const AtmosphereEnvironmentConfig& environment,
                                                std::uint32_t face_index) {
    if (!pipelines_created()) {
        throw std::runtime_error("atmosphere reflection probe pipelines are not initialized");
    }
    const std::array<ViewRayBasis3D, 6> face_view_rays =
        atmosphere_reflection_probe_cube_face_view_rays();
    AtmosphereEnvironmentConfig sky_environment = environment;
    sky_environment.ground_mode = AtmosphereEnvironmentGroundMode::SkyOnly;
    sky_environment.reference_geometry_enabled = false;
    const FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>& material =
        sky_face_material(face_index);
    material.upload(frame_slot,
                    atmosphere_environment_frame_uniforms(
                        sky_environment, {
                                             .view_rays = face_view_rays.at(face_index),
                                             .render_view = AtmosphereEnvironmentRenderView::Final,
                                         }));

    transition_face(recorder, sky_radiance_cube_->handle(), 0, face_index,
                    current_sky_layout(face_index), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    sky_face_initialized_.at(face_index) ? VK_ACCESS_SHADER_READ_BIT : 0,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    sky_face_initialized_.at(face_index) ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                         : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    atmosphere_frame_.record_pass(
        recorder,
        face_target(*sky_radiance_cube_, extent_, format_, sky_face_views_, 0, face_index),
        material.set(frame_slot));
    transition_face(recorder, sky_radiance_cube_->handle(), 0, face_index,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    sky_face_initialized_.at(face_index) = true;
}

void AtmosphereReflectionProbe::record_prefilter_face_mip(
    const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot, std::uint32_t face_index,
    std::uint32_t mip_level) {
    if (!pipelines_created()) {
        throw std::runtime_error("atmosphere reflection probe pipelines are not initialized");
    }
    const std::array<ViewRayBasis3D, 6> face_view_rays =
        atmosphere_reflection_probe_cube_face_view_rays();
    const float roughness =
        mip_levels_ == 1U ? 0.0F
                          : static_cast<float>(mip_level) / static_cast<float>(mip_levels_ - 1U);
    const FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>& material =
        prefilter_material(mip_level, face_index);
    material.upload(frame_slot,
                    prefilter_uniforms(face_view_rays.at(face_index), roughness, mip_level));

    const std::size_t index = face_mip_index(mip_level, face_index);
    transition_face(
        recorder, prefiltered_cube_->handle(), mip_level, face_index,
        current_prefiltered_layout(mip_level, face_index), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        prefiltered_face_mip_initialized_.at(index) ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        prefiltered_face_mip_initialized_.at(index) ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                    : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    record_render_target_pass(
        recorder,
        render_target_view(face_target(*prefiltered_cube_, extent_, format_,
                                       prefiltered_face_views_, mip_level, face_index)),
        RenderClearValues{.color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F)},
        [this, frame_slot, &material](const cubey::vulkan::CommandRecorder& pass_recorder) {
            record_fullscreen_pipeline_draw(pass_recorder,
                                            {
                                                .pipeline = &prefilter_pipeline_.value(),
                                                .descriptor_set = material.set(frame_slot),
                                                .descriptor_set_index = 0,
                                            });
        });
    transition_face(recorder, prefiltered_cube_->handle(), mip_level, face_index,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    prefiltered_face_mip_initialized_.at(index) = true;
}

const FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>&
AtmosphereReflectionProbe::sky_face_material(std::uint32_t face_index) const {
    if (face_index >= sky_face_materials_.size() || sky_face_materials_.at(face_index) == nullptr) {
        throw std::runtime_error(
            "atmosphere reflection probe sky face material is not initialized");
    }
    return *sky_face_materials_.at(face_index);
}

const FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>&
AtmosphereReflectionProbe::prefilter_material(std::uint32_t mip_level,
                                              std::uint32_t face_index) const {
    const std::size_t index = face_mip_index(mip_level, face_index);
    if (mip_level >= mip_levels_ || face_index >= 6U || index >= prefilter_materials_.size() ||
        prefilter_materials_.at(index) == nullptr) {
        throw std::runtime_error(
            "atmosphere reflection probe prefilter material is not initialized");
    }
    return *prefilter_materials_.at(index);
}

void AtmosphereReflectionProbe::transition_face(const cubey::vulkan::CommandRecorder& recorder,
                                                VkImage image, std::uint32_t mip_level,
                                                std::uint32_t face_index, VkImageLayout old_layout,
                                                VkImageLayout new_layout, VkAccessFlags src_access,
                                                VkAccessFlags dst_access,
                                                VkPipelineStageFlags src_stage,
                                                VkPipelineStageFlags dst_stage) const {
    recorder.transition_image_layout(cubey::vulkan::ImageLayoutTransition{
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = old_layout,
        .new_layout = new_layout,
        .src_access_mask = src_access,
        .dst_access_mask = dst_access,
        .src_stage_mask = src_stage,
        .dst_stage_mask = dst_stage,
        .base_mip_level = mip_level,
        .level_count = 1,
        .base_array_layer = face_index,
        .layer_count = 1,
    });
}

VkImageLayout AtmosphereReflectionProbe::current_sky_layout(std::uint32_t face_index) const {
    return sky_face_initialized_.at(face_index) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                : VK_IMAGE_LAYOUT_UNDEFINED;
}

VkImageLayout
AtmosphereReflectionProbe::current_prefiltered_layout(std::uint32_t mip_level,
                                                      std::uint32_t face_index) const {
    const std::size_t index = face_mip_index(mip_level, face_index);
    return prefiltered_face_mip_initialized_.at(index) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_UNDEFINED;
}

} // namespace cubey::render
