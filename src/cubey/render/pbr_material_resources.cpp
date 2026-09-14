#include <cubey/render/pbr_material_resources.h>

#include <cubey/vulkan/upload_batch.h>

#include <stdexcept>

namespace cubey::render {
namespace {

constexpr std::array<PbrMaterialBinding, 17> kSampledMaterialBindings{
    PbrMaterialBinding::BaseColor,
    PbrMaterialBinding::MetallicRoughness,
    PbrMaterialBinding::Normal,
    PbrMaterialBinding::Occlusion,
    PbrMaterialBinding::Emissive,
    PbrMaterialBinding::Specular,
    PbrMaterialBinding::SpecularColor,
    PbrMaterialBinding::Clearcoat,
    PbrMaterialBinding::ClearcoatRoughness,
    PbrMaterialBinding::ClearcoatNormal,
    PbrMaterialBinding::SheenColor,
    PbrMaterialBinding::SheenRoughness,
    PbrMaterialBinding::Anisotropy,
    PbrMaterialBinding::Iridescence,
    PbrMaterialBinding::IridescenceThickness,
    PbrMaterialBinding::Transmission,
    PbrMaterialBinding::VolumeThickness,
};

constexpr std::array<PbrDefaultTextureSpec, 17> kDefaultTextureSpecs{
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::BaseColor,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_SRGB,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::MetallicRoughness,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::Normal,
        .rgba8 = {128, 128, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::Occlusion,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::Emissive,
        .rgba8 = {0, 0, 0, 255},
        .format = VK_FORMAT_R8G8B8A8_SRGB,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::Specular,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::SpecularColor,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_SRGB,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::Clearcoat,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::ClearcoatRoughness,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::ClearcoatNormal,
        .rgba8 = {128, 128, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::SheenColor,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_SRGB,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::SheenRoughness,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::Anisotropy,
        .rgba8 = {255, 128, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::Iridescence,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::IridescenceThickness,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::Transmission,
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
    PbrDefaultTextureSpec{
        .binding = PbrMaterialBinding::VolumeThickness,
        // KHR_materials_volume reads the G channel. White preserves authored
        // thickness when no texture was supplied.
        .rgba8 = {255, 255, 255, 255},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    },
};

[[nodiscard]] Texture2D create_pbr_default_texture(const cubey::vulkan::Device& device,
                                                   cubey::vulkan::GpuRuntime& gpu,
                                                   const PbrDefaultTextureSpec& spec) {
    return create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {1, 1},
            .format = spec.format,
            .rgba8 = std::span<const std::uint8_t>{spec.rgba8.data(), spec.rgba8.size()},
            .create_sampler = true,
            .sampler = {},
        });
}

[[nodiscard]] Texture2D create_pbr_default_texture(const cubey::vulkan::Device& device,
                                                   cubey::vulkan::GpuOwnerContext& gpu,
                                                   const PbrDefaultTextureSpec& spec) {
    return create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {1, 1},
            .format = spec.format,
            .rgba8 = std::span<const std::uint8_t>{spec.rgba8.data(), spec.rgba8.size()},
            .create_sampler = true,
            .sampler = {},
        });
}

[[nodiscard]] Texture2D create_pbr_default_texture(const cubey::vulkan::Device& device,
                                                   cubey::vulkan::GpuUploadBatch& batch,
                                                   const PbrDefaultTextureSpec& spec) {
    return create_uploaded_texture_2d(
        device, batch,
        {
            .extent = {1, 1},
            .format = spec.format,
            .rgba8 = std::span<const std::uint8_t>{spec.rgba8.data(), spec.rgba8.size()},
            .create_sampler = true,
            .sampler = {},
        });
}

} // namespace

std::span<const PbrMaterialBinding> pbr_sampled_material_bindings() noexcept {
    return kSampledMaterialBindings;
}

std::span<const PbrDefaultTextureSpec> pbr_default_texture_specs() noexcept {
    return kDefaultTextureSpecs;
}

PbrDefaultTextureSet create_pbr_default_texture_set(const cubey::vulkan::Device& device,
                                                    cubey::vulkan::GpuRuntime& gpu) {
    return {
        .base_color = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[0]),
        .metallic_roughness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[1]),
        .normal = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[2]),
        .occlusion = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[3]),
        .emissive = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[4]),
        .specular = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[5]),
        .specular_color = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[6]),
        .clearcoat = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[7]),
        .clearcoat_roughness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[8]),
        .clearcoat_normal = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[9]),
        .sheen_color = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[10]),
        .sheen_roughness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[11]),
        .anisotropy = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[12]),
        .iridescence = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[13]),
        .iridescence_thickness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[14]),
        .transmission = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[15]),
        .volume_thickness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[16]),
    };
}

PbrDefaultTextureSet create_pbr_default_texture_set(const cubey::vulkan::Device& device,
                                                    cubey::vulkan::GpuOwnerContext& gpu) {
    return {
        .base_color = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[0]),
        .metallic_roughness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[1]),
        .normal = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[2]),
        .occlusion = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[3]),
        .emissive = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[4]),
        .specular = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[5]),
        .specular_color = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[6]),
        .clearcoat = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[7]),
        .clearcoat_roughness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[8]),
        .clearcoat_normal = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[9]),
        .sheen_color = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[10]),
        .sheen_roughness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[11]),
        .anisotropy = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[12]),
        .iridescence = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[13]),
        .iridescence_thickness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[14]),
        .transmission = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[15]),
        .volume_thickness = create_pbr_default_texture(device, gpu, kDefaultTextureSpecs[16]),
    };
}

PbrDefaultTextureSet create_pbr_default_texture_set(const cubey::vulkan::Device& device,
                                                    cubey::vulkan::GpuUploadBatch& batch) {
    return {
        .base_color = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[0]),
        .metallic_roughness = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[1]),
        .normal = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[2]),
        .occlusion = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[3]),
        .emissive = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[4]),
        .specular = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[5]),
        .specular_color = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[6]),
        .clearcoat = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[7]),
        .clearcoat_roughness = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[8]),
        .clearcoat_normal = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[9]),
        .sheen_color = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[10]),
        .sheen_roughness = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[11]),
        .anisotropy = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[12]),
        .iridescence = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[13]),
        .iridescence_thickness =
            create_pbr_default_texture(device, batch, kDefaultTextureSpecs[14]),
        .transmission = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[15]),
        .volume_thickness = create_pbr_default_texture(device, batch, kDefaultTextureSpecs[16]),
    };
}

PbrDefaultTextureSet make_pbr_default_texture_set(std::vector<Texture2D> textures) {
    if (textures.size() != kDefaultTextureSpecs.size()) {
        throw std::runtime_error("PBR default texture set requires every sampled binding");
    }
    return {
        .base_color = std::move(textures[0]),
        .metallic_roughness = std::move(textures[1]),
        .normal = std::move(textures[2]),
        .occlusion = std::move(textures[3]),
        .emissive = std::move(textures[4]),
        .specular = std::move(textures[5]),
        .specular_color = std::move(textures[6]),
        .clearcoat = std::move(textures[7]),
        .clearcoat_roughness = std::move(textures[8]),
        .clearcoat_normal = std::move(textures[9]),
        .sheen_color = std::move(textures[10]),
        .sheen_roughness = std::move(textures[11]),
        .anisotropy = std::move(textures[12]),
        .iridescence = std::move(textures[13]),
        .iridescence_thickness = std::move(textures[14]),
        .transmission = std::move(textures[15]),
        .volume_thickness = std::move(textures[16]),
    };
}

const Texture2D& pbr_default_texture(const PbrDefaultTextureSet& set, PbrMaterialBinding binding) {
    switch (binding) {
    case PbrMaterialBinding::BaseColor:
        return set.base_color;
    case PbrMaterialBinding::MetallicRoughness:
        return set.metallic_roughness;
    case PbrMaterialBinding::Normal:
        return set.normal;
    case PbrMaterialBinding::Occlusion:
        return set.occlusion;
    case PbrMaterialBinding::Emissive:
        return set.emissive;
    case PbrMaterialBinding::Specular:
        return set.specular;
    case PbrMaterialBinding::SpecularColor:
        return set.specular_color;
    case PbrMaterialBinding::Clearcoat:
        return set.clearcoat;
    case PbrMaterialBinding::ClearcoatRoughness:
        return set.clearcoat_roughness;
    case PbrMaterialBinding::ClearcoatNormal:
        return set.clearcoat_normal;
    case PbrMaterialBinding::SheenColor:
        return set.sheen_color;
    case PbrMaterialBinding::SheenRoughness:
        return set.sheen_roughness;
    case PbrMaterialBinding::Anisotropy:
        return set.anisotropy;
    case PbrMaterialBinding::Iridescence:
        return set.iridescence;
    case PbrMaterialBinding::IridescenceThickness:
        return set.iridescence_thickness;
    case PbrMaterialBinding::Transmission:
        return set.transmission;
    case PbrMaterialBinding::VolumeThickness:
        return set.volume_thickness;
    case PbrMaterialBinding::Uniforms:
        break;
    }
    throw std::runtime_error("PBR material binding is not a sampled texture");
}

std::vector<SampledImageMaterialBinding>
pbr_default_sampled_image_bindings(const PbrDefaultTextureSet& set) {
    std::vector<SampledImageMaterialBinding> bindings;
    bindings.reserve(kSampledMaterialBindings.size());
    for (const PbrMaterialBinding binding : kSampledMaterialBindings) {
        const Texture2D& texture = pbr_default_texture(set, binding);
        bindings.push_back({
            .binding = static_cast<std::uint32_t>(binding),
            .sampler = texture.sampler().handle(),
            .image_view = texture.view(),
        });
    }
    return bindings;
}

bool PbrMaterialTable::contains(MaterialHandle material) const {
    return records_.contains(material);
}

const PbrMaterialDefinition& PbrMaterialTable::definition(MaterialHandle material) const {
    return records_.at(material).definition();
}

FrameUniformMaterialInstance<PbrMaterialUniforms>&
PbrMaterialTable::emplace(MaterialHandle material, PbrMaterialDefinition definition,
                          const cubey::vulkan::Device& device,
                          const FrameUniformMaterialInstanceConfig& instance_config) {
    if (!material) {
        throw std::runtime_error("PBR material table insert requires a non-null handle");
    }
    if (records_.contains(material)) {
        throw std::runtime_error("PBR material table already contains handle");
    }

    const VkDescriptorSetLayout previous_layout = descriptor_set_layout_;
    bool inserted = false;
    try {
        PbrMaterialRecord& record =
            records_.emplace(material, std::move(definition), device, instance_config);
        inserted = true;
        register_descriptor_set_layout(record.instance().layout());
        return record.instance();
    } catch (...) {
        if (inserted) {
            records_.erase(material);
        }
        descriptor_set_layout_ = previous_layout;
        throw;
    }
}

FrameUniformMaterialInstance<PbrMaterialUniforms>&
PbrMaterialTable::instance(MaterialHandle material) {
    return records_.at(material).instance();
}

const FrameUniformMaterialInstance<PbrMaterialUniforms>&
PbrMaterialTable::instance(MaterialHandle material) const {
    return records_.at(material).instance();
}

void PbrMaterialTable::register_descriptor_set_layout(VkDescriptorSetLayout layout) {
    if (layout == VK_NULL_HANDLE) {
        throw std::runtime_error("PBR material table instance requires a descriptor set layout");
    }
    if (descriptor_set_layout_ == VK_NULL_HANDLE) {
        descriptor_set_layout_ = layout;
    }
}

VkDescriptorSetLayout PbrMaterialTable::descriptor_set_layout() const {
    if (descriptor_set_layout_ == VK_NULL_HANDLE) {
        throw std::runtime_error("PBR material table requires at least one material instance");
    }
    return descriptor_set_layout_;
}

VkDescriptorSetLayout PbrMaterialTable::layout(MaterialHandle material) const {
    return instance(material).layout();
}

void PbrMaterialTable::upload(MaterialHandle material, FrameSlot frame_slot) const {
    instance(material).upload(frame_slot, pbr_material_uniforms(definition(material)));
}

void PbrMaterialTable::rebind(MaterialHandle from, MaterialHandle to) {
    if (!contains(from)) {
        throw std::runtime_error("PBR material table rebind requires an existing source handle");
    }
    if (from == to) {
        return;
    }
    if (!to) {
        throw std::runtime_error(
            "PBR material table rebind requires a non-null destination handle");
    }
    if (contains(to)) {
        throw std::runtime_error("PBR material table rebind destination already exists");
    }
    records_.rebind(from, to);
}

void PbrMaterialTable::erase(MaterialHandle material) {
    if (!contains(material)) {
        throw std::runtime_error("PBR material table erase requires an existing handle");
    }
    const VkDescriptorSetLayout erased_layout = instance(material).layout();
    records_.erase(material);
    if (records_.empty()) {
        descriptor_set_layout_ = VK_NULL_HANDLE;
    } else if (descriptor_set_layout_ == erased_layout) {
        descriptor_set_layout_ = records_.first().instance().layout();
    }
}

void PbrMaterialTable::clear() {
    records_.clear();
    descriptor_set_layout_ = VK_NULL_HANDLE;
}

} // namespace cubey::render
