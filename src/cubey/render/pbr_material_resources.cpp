#include <cubey/render/pbr_material_resources.h>

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/upload_batch.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace cubey::render {
namespace {

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

std::span<const PbrDefaultTextureSpec> pbr_default_texture_specs() noexcept {
    return kDefaultTextureSpecs;
}

void validate_pbr_sampled_image_bindings(
    std::span<const SampledImageMaterialBinding> sampled_images) {
    const std::span<const PbrMaterialBinding> expected = pbr_sampled_material_bindings();
    if (sampled_images.size() != expected.size()) {
        throw std::runtime_error(
            "PBR material publication requires every sampled texture binding exactly once");
    }

    std::array<bool, 17> seen{};
    for (const SampledImageMaterialBinding& sampled : sampled_images) {
        const auto expected_binding =
            std::find_if(expected.begin(), expected.end(), [&sampled](PbrMaterialBinding binding) {
                return sampled.binding == static_cast<std::uint32_t>(binding);
            });
        if (expected_binding == expected.end()) {
            throw std::runtime_error(
                "PBR material publication contains an unknown sampled binding");
        }

        const std::size_t expected_index =
            static_cast<std::size_t>(expected_binding - expected.begin());
        if (seen[expected_index]) {
            throw std::runtime_error(
                "PBR material publication contains a duplicate sampled binding");
        }
        seen[expected_index] = true;

        if (sampled.sampler == VK_NULL_HANDLE || sampled.image_view == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "PBR material publication requires sampled image sampler and view handles");
        }
    }
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
    const std::span<const PbrMaterialBinding> sampled_bindings = pbr_sampled_material_bindings();
    bindings.reserve(sampled_bindings.size());
    for (const PbrMaterialBinding binding : sampled_bindings) {
        const Texture2D& texture = pbr_default_texture(set, binding);
        bindings.push_back({
            .binding = static_cast<std::uint32_t>(binding),
            .sampler = texture.sampler().handle(),
            .image_view = texture.view(),
        });
    }
    return bindings;
}

PbrMaterialUniformBlockLayout
pbr_material_uniform_block_layout(VkDeviceSize uniform_byte_size,
                                  VkDeviceSize min_uniform_buffer_offset_alignment,
                                  std::uint32_t material_capacity) {
    if (uniform_byte_size == 0U) {
        throw std::runtime_error("PBR material uniform byte size must be positive");
    }
    if (material_capacity == 0U) {
        throw std::runtime_error("PBR material block capacity must be positive");
    }

    const VkDeviceSize alignment = std::max<VkDeviceSize>(min_uniform_buffer_offset_alignment, 1U);
    const VkDeviceSize remainder = uniform_byte_size % alignment;
    const VkDeviceSize padding = remainder == 0U ? 0U : alignment - remainder;
    if (uniform_byte_size > std::numeric_limits<VkDeviceSize>::max() - padding) {
        throw std::runtime_error("PBR material uniform stride overflows device size");
    }
    const VkDeviceSize uniform_stride = uniform_byte_size + padding;
    if (uniform_stride > std::numeric_limits<VkDeviceSize>::max() / material_capacity) {
        throw std::runtime_error("PBR material uniform block byte size overflows device size");
    }
    return {
        .uniform_stride = uniform_stride,
        .uniform_block_byte_size = uniform_stride * material_capacity,
    };
}

namespace {

[[nodiscard]] PbrMaterialTableConfig
validated_pbr_material_table_config(PbrMaterialTableConfig config) {
    if (config.block_capacity == 0U) {
        throw std::runtime_error("PBR material table block capacity must be positive");
    }
    return config;
}

void validate_pbr_uniform_range(const cubey::vulkan::Device& device) {
    if (sizeof(PbrMaterialUniforms) > device.properties().limits.maxUniformBufferRange) {
        throw std::runtime_error("PBR material uniforms exceed maxUniformBufferRange");
    }
}

} // namespace

PbrMaterialRecord::PbrMaterialRecord(PbrMaterialDefinition definition,
                                     VkDescriptorSet descriptor_set,
                                     std::uint32_t descriptor_set_index,
                                     VkDeviceSize uniform_offset)
    : definition_(std::move(definition)), descriptor_set_(descriptor_set),
      descriptor_set_index_(descriptor_set_index), uniform_offset_(uniform_offset) {
    if (descriptor_set_ == VK_NULL_HANDLE) {
        throw std::runtime_error("PBR material record requires a static descriptor set");
    }
}

struct PbrMaterialTable::State {
    struct Block {
        struct Allocation {
            VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
            VkDeviceSize uniform_offset = 0;
        };

        Block(const cubey::vulkan::Device& device, const cubey::vulkan::DescriptorSetInfo& info,
              VkDescriptorSetLayout descriptor_set_layout,
              const PbrMaterialUniformBlockLayout& uniform_layout, std::uint32_t material_capacity)
            : uniform_buffer_(device,
                              cubey::vulkan::BufferConfig{
                                  .size = uniform_layout.uniform_block_byte_size,
                                  .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              }),
              descriptor_pool_(device, info.pool_info()),
              descriptor_set_layout_(descriptor_set_layout), uniform_layout_(uniform_layout),
              material_capacity_(material_capacity) {
            static_cast<void>(uniform_buffer_.map_persistent());
        }

        [[nodiscard]] bool full() const noexcept {
            return allocated_descriptor_set_count_ == material_capacity_;
        }

        [[nodiscard]] std::uint32_t allocated_descriptor_set_count() const noexcept {
            return allocated_descriptor_set_count_;
        }

        [[nodiscard]] Allocation allocate() {
            if (full()) {
                throw std::runtime_error("PBR material block has no remaining descriptor slots");
            }
            const VkDescriptorSet descriptor_set =
                descriptor_pool_.allocate(descriptor_set_layout_);
            const VkDeviceSize uniform_offset =
                uniform_layout_.uniform_stride * allocated_descriptor_set_count_;
            ++allocated_descriptor_set_count_;
            return {
                .descriptor_set = descriptor_set,
                .uniform_offset = uniform_offset,
            };
        }

        void write_uniforms(const PbrMaterialUniforms& uniforms,
                            VkDeviceSize uniform_offset) const {
            uniform_buffer_.upload(&uniforms, sizeof(uniforms), uniform_offset);
        }

        [[nodiscard]] VkBuffer uniform_buffer() const noexcept {
            return uniform_buffer_.handle();
        }

      private:
        // Destruction is intentionally pool before buffer: both belong to the
        // block, and all descriptor references die before their backing memory.
        cubey::vulkan::Buffer uniform_buffer_;
        cubey::vulkan::DescriptorPool descriptor_pool_;
        VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
        PbrMaterialUniformBlockLayout uniform_layout_{};
        std::uint32_t material_capacity_ = 0;
        std::uint32_t allocated_descriptor_set_count_ = 0;
    };

    State(const cubey::vulkan::Device& device, PbrMaterialTableConfig config)
        : device_(&device), config_(validated_pbr_material_table_config(std::move(config))),
          uniform_layout_(pbr_material_uniform_block_layout(
              sizeof(PbrMaterialUniforms),
              device.properties().limits.minUniformBufferOffsetAlignment, config_.block_capacity)),
          descriptor_schema_(pbr_material_descriptor_set_layout()),
          descriptor_info_(descriptor_schema_.bindings, config_.block_capacity),
          descriptor_set_layout_(device, descriptor_info_.layout_info()) {}

    [[nodiscard]] Block& append_block() {
        blocks_.push_back(std::make_unique<Block>(*device_, descriptor_info_,
                                                  descriptor_set_layout_.handle(), uniform_layout_,
                                                  config_.block_capacity));
        return *blocks_.back();
    }

    const cubey::vulkan::Device* device_ = nullptr;
    PbrMaterialTableConfig config_{};
    PbrMaterialUniformBlockLayout uniform_layout_{};
    MaterialDescriptorSetLayout descriptor_schema_{};
    cubey::vulkan::DescriptorSetInfo descriptor_info_;
    cubey::vulkan::DescriptorSetLayout descriptor_set_layout_;
    std::vector<std::unique_ptr<Block>> blocks_{};
    std::uint32_t material_count_ = 0;
};

PbrMaterialTable::PbrMaterialTable() = default;
PbrMaterialTable::~PbrMaterialTable() = default;
PbrMaterialTable::PbrMaterialTable(PbrMaterialTable&&) noexcept = default;
PbrMaterialTable& PbrMaterialTable::operator=(PbrMaterialTable&&) noexcept = default;

void PbrMaterialTable::initialize(const cubey::vulkan::Device& device,
                                  PbrMaterialTableConfig config) {
    if (state_ != nullptr) {
        throw std::runtime_error("PBR material table is already initialized");
    }
    if (!records_.empty()) {
        throw std::runtime_error("PBR material table cannot initialize after publication");
    }
    validate_pbr_uniform_range(device);
    state_ = std::make_unique<State>(device, std::move(config));
}

bool PbrMaterialTable::initialized() const noexcept {
    return state_ != nullptr;
}

bool PbrMaterialTable::contains(MaterialHandle material) const {
    return records_.contains(material);
}

const PbrMaterialDefinition& PbrMaterialTable::definition(MaterialHandle material) const {
    return records_.at(material).definition();
}

PbrMaterialRecord&
PbrMaterialTable::emplace(MaterialHandle material, PbrMaterialDefinition definition,
                          std::span<const SampledImageMaterialBinding> sampled_images) {
    if (state_ == nullptr) {
        throw std::runtime_error("PBR material table requires initialization before publication");
    }
    if (!material) {
        throw std::runtime_error("PBR material table insert requires a non-null handle");
    }
    if (records_.contains(material)) {
        throw std::runtime_error("PBR material table already contains handle");
    }
    validate_pbr_sampled_image_bindings(sampled_images);

    State& state = *state_;
    const bool needs_block = state.blocks_.empty() || state.blocks_.back()->full();
    if (needs_block) {
        static_cast<void>(state.append_block());
    }

    try {
        State::Block& block = *state.blocks_.back();
        const State::Block::Allocation allocation = block.allocate();
        const PbrMaterialUniforms uniforms = pbr_material_uniforms(definition);
        block.write_uniforms(uniforms, allocation.uniform_offset);

        cubey::vulkan::DescriptorWriteBatch writes;
        writes.uniform_buffer(allocation.descriptor_set,
                              static_cast<std::uint32_t>(PbrMaterialBinding::Uniforms),
                              block.uniform_buffer(), sizeof(uniforms), allocation.uniform_offset);
        for (const SampledImageMaterialBinding& sampled : sampled_images) {
            writes.combined_image_sampler(allocation.descriptor_set, sampled.binding,
                                          sampled.sampler, sampled.image_view, sampled.layout);
        }
        writes.update(*state.device_);
        PbrMaterialRecord& record =
            records_.emplace(material, std::move(definition), allocation.descriptor_set,
                             state.descriptor_schema_.set, allocation.uniform_offset);
        ++state.material_count_;
        return record;
    } catch (...) {
        // A freshly created block cannot contain another logical material, so
        // discard it entirely on failed first publication. Existing blocks
        // intentionally keep consumed physical slots until clear().
        if (needs_block) {
            state.blocks_.pop_back();
        }
        throw;
    }
}

PbrMaterialRecord& PbrMaterialTable::record(MaterialHandle material) {
    return records_.at(material);
}

const PbrMaterialRecord& PbrMaterialTable::record(MaterialHandle material) const {
    return records_.at(material);
}

VkDescriptorSetLayout PbrMaterialTable::descriptor_set_layout() const {
    if (state_ == nullptr) {
        throw std::runtime_error("PBR material table requires initialization before layout access");
    }
    return state_->descriptor_set_layout_.handle();
}

PbrMaterialTableMetrics PbrMaterialTable::metrics() const {
    if (state_ == nullptr) {
        return {};
    }

    PbrMaterialTableMetrics result{
        .initialized = true,
        .material_count = state_->material_count_,
        .allocated_descriptor_set_count = 0,
        .block_count = static_cast<std::uint32_t>(state_->blocks_.size()),
        .descriptor_pool_count = static_cast<std::uint32_t>(state_->blocks_.size()),
        .uniform_buffer_count = static_cast<std::uint32_t>(state_->blocks_.size()),
        .block_capacity = state_->config_.block_capacity,
        .uniform_stride = state_->uniform_layout_.uniform_stride,
        .uniform_block_byte_size = state_->uniform_layout_.uniform_block_byte_size,
        .allocated_uniform_byte_size = 0,
    };
    for (const std::unique_ptr<State::Block>& block : state_->blocks_) {
        if (result.allocated_descriptor_set_count >
            std::numeric_limits<std::uint32_t>::max() - block->allocated_descriptor_set_count()) {
            throw std::runtime_error("PBR material descriptor set metric overflows");
        }
        result.allocated_descriptor_set_count += block->allocated_descriptor_set_count();
        if (result.allocated_uniform_byte_size >
            std::numeric_limits<VkDeviceSize>::max() - result.uniform_block_byte_size) {
            throw std::runtime_error("PBR material uniform byte metric overflows");
        }
        result.allocated_uniform_byte_size += result.uniform_block_byte_size;
    }
    return result;
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
    records_.erase(material);
    --state_->material_count_;
}

void PbrMaterialTable::clear() {
    records_.clear();
    state_.reset();
}

void bind_pbr_material(const cubey::vulkan::CommandRecorder& recorder,
                       const GraphicsPipelineResource& pipeline,
                       const PbrMaterialRecord& material) {
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(),
                                 material.descriptor_set_index(), material.descriptor_set());
}

} // namespace cubey::render
