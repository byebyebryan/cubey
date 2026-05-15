#include <cubey/render/pbr_material_resources.h>

#include <stdexcept>

namespace cubey::render {
namespace {

constexpr std::array<PbrMaterialBinding, 15> kSampledMaterialBindings{
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
};

constexpr std::array<PbrDefaultTextureSpec, 15> kDefaultTextureSpecs{
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

bool PbrMaterialTable::contains_factors(MaterialHandle material) const {
    return factors_.contains(material);
}

bool PbrMaterialTable::contains_instance(MaterialHandle material) const {
    return instances_.contains(material);
}

bool PbrMaterialTable::contains(MaterialHandle material) const {
    return contains_factors(material) && contains_instance(material);
}

void PbrMaterialTable::set_factors(MaterialHandle material, const PbrMaterialFactors& factors) {
    if (!material) {
        throw std::runtime_error("PBR material table factors require a non-null handle");
    }
    factors_.insert_or_assign(material, factors);
}

PbrMaterialFactors& PbrMaterialTable::factors(MaterialHandle material) {
    const auto position = factors_.find(material);
    if (position == factors_.end()) {
        throw std::runtime_error("PBR material table does not contain factors");
    }
    return position->second;
}

const PbrMaterialFactors& PbrMaterialTable::factors(MaterialHandle material) const {
    const auto position = factors_.find(material);
    if (position == factors_.end()) {
        throw std::runtime_error("PBR material table does not contain factors");
    }
    return position->second;
}

FrameUniformMaterialInstance<PbrMaterialUniforms>&
PbrMaterialTable::instance(MaterialHandle material) {
    return instances_.at(material);
}

const FrameUniformMaterialInstance<PbrMaterialUniforms>&
PbrMaterialTable::instance(MaterialHandle material) const {
    return instances_.at(material);
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
    const PbrMaterialFactors& material_factors = factors(material);
    upload(material, frame_slot, material_factors.alpha_mode);
}

void PbrMaterialTable::upload(MaterialHandle material, FrameSlot frame_slot,
                              MaterialAlphaMode alpha_mode) const {
    instance(material).upload(frame_slot, pbr_material_uniforms(factors(material), alpha_mode));
}

void PbrMaterialTable::erase(MaterialHandle material) {
    bool erased = false;
    if (instances_.contains(material)) {
        instances_.erase(material);
        if (instances_.empty()) {
            descriptor_set_layout_ = VK_NULL_HANDLE;
        }
        erased = true;
    }
    erased = factors_.erase(material) > 0 || erased;
    if (!erased) {
        throw std::runtime_error("PBR material table erase requires an existing handle");
    }
}

void PbrMaterialTable::clear() {
    instances_.clear();
    factors_.clear();
    descriptor_set_layout_ = VK_NULL_HANDLE;
}

} // namespace cubey::render
