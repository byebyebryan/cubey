#include <cubey/vulkan/descriptors.h>

#include <cubey/vulkan/vk_check.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace cubey::vulkan {

VkWriteDescriptorSet DescriptorBufferWrite::descriptor_write() const& {
    auto result = vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    result.dstSet = set;
    result.dstBinding = binding;
    result.descriptorCount = 1;
    result.descriptorType = descriptor_type;
    result.pBufferInfo = &buffer_info;
    return result;
}

VkWriteDescriptorSet DescriptorImageWrite::descriptor_write() const& {
    auto result = vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    result.dstSet = set;
    result.dstBinding = binding;
    result.descriptorCount = 1;
    result.descriptorType = descriptor_type;
    result.pImageInfo = &image_info;
    return result;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
VkDescriptorSetLayoutBinding descriptor_binding(std::uint32_t binding, VkDescriptorType type,
                                                VkShaderStageFlags stage_flags,
                                                std::uint32_t descriptor_count) {
    if (descriptor_count == 0) {
        throw std::runtime_error("descriptor binding count must be positive");
    }

    VkDescriptorSetLayoutBinding result{};
    result.binding = binding;
    result.descriptorType = type;
    result.descriptorCount = descriptor_count;
    result.stageFlags = stage_flags;
    return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

VkDescriptorSetLayoutCreateInfo
descriptor_set_layout_info(std::span<const VkDescriptorSetLayoutBinding> bindings) {
    auto info = vk_struct<VkDescriptorSetLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    return info;
}

VkDescriptorPoolSize descriptor_pool_size(VkDescriptorType type, std::uint32_t descriptor_count) {
    if (descriptor_count == 0) {
        throw std::runtime_error("descriptor pool size count must be positive");
    }

    VkDescriptorPoolSize result{};
    result.type = type;
    result.descriptorCount = descriptor_count;
    return result;
}

VkDescriptorPoolCreateInfo descriptor_pool_info(std::uint32_t max_sets,
                                                std::span<const VkDescriptorPoolSize> pool_sizes) {
    if (max_sets == 0) {
        throw std::runtime_error("descriptor pool max set count must be positive");
    }
    if (pool_sizes.empty()) {
        throw std::runtime_error("descriptor pool requires at least one pool size");
    }

    auto info =
        vk_struct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    info.maxSets = max_sets;
    info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
    info.pPoolSizes = pool_sizes.data();
    return info;
}

VkDescriptorSetAllocateInfo
descriptor_set_allocate_info(VkDescriptorPool pool,
                             std::span<const VkDescriptorSetLayout> layouts) {
    if (pool == VK_NULL_HANDLE) {
        throw std::runtime_error("descriptor set allocation requires a valid pool");
    }
    if (layouts.empty()) {
        throw std::runtime_error("descriptor set allocation requires at least one layout");
    }
    if (layouts.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("descriptor set allocation layout count overflow");
    }
    for (VkDescriptorSetLayout layout : layouts) {
        if (layout == VK_NULL_HANDLE) {
            throw std::runtime_error("descriptor set allocation requires valid layouts");
        }
    }

    auto info =
        vk_struct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    info.descriptorPool = pool;
    info.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
    info.pSetLayouts = layouts.data();
    return info;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
DescriptorBufferWrite uniform_buffer_descriptor(VkDescriptorSet set, std::uint32_t binding,
                                                VkBuffer buffer, VkDeviceSize range,
                                                VkDeviceSize offset) {
    if (set == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("uniform buffer descriptor requires valid set and buffer");
    }
    if (range == 0) {
        throw std::runtime_error("uniform buffer descriptor range must be positive");
    }

    DescriptorBufferWrite result{};
    result.buffer_info.buffer = buffer;
    result.buffer_info.offset = offset;
    result.buffer_info.range = range;
    result.set = set;
    result.binding = binding;
    result.descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
DescriptorBufferWrite storage_buffer_descriptor(VkDescriptorSet set, std::uint32_t binding,
                                                VkBuffer buffer, VkDeviceSize range,
                                                VkDeviceSize offset) {
    if (set == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("storage buffer descriptor requires valid set and buffer");
    }
    if (range == 0) {
        throw std::runtime_error("storage buffer descriptor range must be positive");
    }

    DescriptorBufferWrite result{};
    result.buffer_info.buffer = buffer;
    result.buffer_info.offset = offset;
    result.buffer_info.range = range;
    result.set = set;
    result.binding = binding;
    result.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

DescriptorImageWrite storage_image_descriptor(VkDescriptorSet set, std::uint32_t binding,
                                              VkImageView image_view, VkImageLayout layout) {
    if (set == VK_NULL_HANDLE || image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("storage image descriptor requires valid set and image view");
    }

    DescriptorImageWrite result{};
    result.image_info.imageView = image_view;
    result.image_info.imageLayout = layout;
    result.set = set;
    result.binding = binding;
    result.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    return result;
}

DescriptorImageWrite combined_image_sampler_descriptor(VkDescriptorSet set, std::uint32_t binding,
                                                       VkSampler sampler, VkImageView image_view,
                                                       VkImageLayout layout) {
    if (set == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE || image_view == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "combined image sampler descriptor requires valid set, sampler, and image view");
    }

    DescriptorImageWrite result{};
    result.image_info.sampler = sampler;
    result.image_info.imageView = image_view;
    result.image_info.imageLayout = layout;
    result.set = set;
    result.binding = binding;
    result.descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    return result;
}

void update_descriptor_sets(const Device& device, std::span<const VkWriteDescriptorSet> writes) {
    if (device.handle() == VK_NULL_HANDLE) {
        throw std::runtime_error("descriptor update requires a valid Vulkan device");
    }
    if (writes.empty()) {
        throw std::runtime_error("descriptor update requires at least one write");
    }

    vkUpdateDescriptorSets(device.handle(), static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
DescriptorWriteBatch& DescriptorWriteBatch::uniform_buffer(VkDescriptorSet set,
                                                           std::uint32_t binding, VkBuffer buffer,
                                                           VkDeviceSize range,
                                                           VkDeviceSize offset) {
    buffer_writes_.push_back(uniform_buffer_descriptor(set, binding, buffer, range, offset));
    write_refs_.push_back({WriteKind::Buffer, buffer_writes_.size() - 1U});
    cached_writes_.clear();
    return *this;
}

DescriptorWriteBatch& DescriptorWriteBatch::storage_buffer(VkDescriptorSet set,
                                                           std::uint32_t binding, VkBuffer buffer,
                                                           VkDeviceSize range,
                                                           VkDeviceSize offset) {
    buffer_writes_.push_back(storage_buffer_descriptor(set, binding, buffer, range, offset));
    write_refs_.push_back({WriteKind::Buffer, buffer_writes_.size() - 1U});
    cached_writes_.clear();
    return *this;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

DescriptorWriteBatch& DescriptorWriteBatch::storage_image(VkDescriptorSet set,
                                                          std::uint32_t binding,
                                                          VkImageView image_view,
                                                          VkImageLayout layout) {
    image_writes_.push_back(storage_image_descriptor(set, binding, image_view, layout));
    write_refs_.push_back({WriteKind::Image, image_writes_.size() - 1U});
    cached_writes_.clear();
    return *this;
}

DescriptorWriteBatch& DescriptorWriteBatch::combined_image_sampler(VkDescriptorSet set,
                                                                   std::uint32_t binding,
                                                                   VkSampler sampler,
                                                                   VkImageView image_view,
                                                                   VkImageLayout layout) {
    image_writes_.push_back(
        combined_image_sampler_descriptor(set, binding, sampler, image_view, layout));
    write_refs_.push_back({WriteKind::Image, image_writes_.size() - 1U});
    cached_writes_.clear();
    return *this;
}

std::span<const VkWriteDescriptorSet> DescriptorWriteBatch::writes() const {
    cached_writes_.clear();
    cached_writes_.reserve(write_refs_.size());

    for (const WriteRef& ref : write_refs_) {
        switch (ref.kind) {
        case WriteKind::Buffer:
            cached_writes_.push_back(buffer_writes_.at(ref.index).descriptor_write());
            break;
        case WriteKind::Image:
            cached_writes_.push_back(image_writes_.at(ref.index).descriptor_write());
            break;
        }
    }

    return cached_writes_;
}

void DescriptorWriteBatch::clear() {
    buffer_writes_.clear();
    image_writes_.clear();
    write_refs_.clear();
    cached_writes_.clear();
}

void DescriptorWriteBatch::update(const Device& device) const {
    update_descriptor_sets(device, writes());
}

DescriptorSetInfo::DescriptorSetInfo(std::span<const DescriptorSetBindingConfig> bindings,
                                     std::uint32_t max_sets) {
    if (bindings.empty()) {
        throw std::runtime_error("descriptor set info requires at least one binding");
    }
    if (max_sets == 0) {
        throw std::runtime_error("descriptor set info max set count must be positive");
    }
    max_sets_ = max_sets;

    bindings_.reserve(bindings.size());
    for (const DescriptorSetBindingConfig& binding : bindings) {
        bindings_.push_back(descriptor_binding(binding.binding, binding.type, binding.stage_flags,
                                               binding.descriptor_count));

        const std::uint32_t descriptor_count = binding.descriptor_count;
        if (descriptor_count > std::numeric_limits<std::uint32_t>::max() / max_sets) {
            throw std::runtime_error("descriptor pool size count overflow");
        }
        const std::uint32_t total_count = descriptor_count * max_sets;

        bool merged = false;
        for (VkDescriptorPoolSize& pool_size : pool_sizes_) {
            if (pool_size.type == binding.type) {
                if (pool_size.descriptorCount >
                    std::numeric_limits<std::uint32_t>::max() - total_count) {
                    throw std::runtime_error("descriptor pool size count overflow");
                }
                pool_size.descriptorCount += total_count;
                merged = true;
                break;
            }
        }
        if (!merged) {
            pool_sizes_.push_back(descriptor_pool_size(binding.type, total_count));
        }
    }

    layout_info_ = descriptor_set_layout_info(bindings_);
    pool_info_ = descriptor_pool_info(max_sets, pool_sizes_);
}

DescriptorSetSchema::DescriptorSetSchema(std::span<const DescriptorSetSchemaBinding> bindings) {
    if (bindings.empty()) {
        throw std::runtime_error("descriptor set schema requires at least one binding");
    }
    bindings_.reserve(bindings.size());
    for (const DescriptorSetSchemaBinding& binding : bindings) {
        if (binding.name.empty()) {
            throw std::runtime_error("descriptor set schema binding name must be non-empty");
        }
        for (const Binding& existing : bindings_) {
            if (existing.name == binding.name) {
                throw std::runtime_error("descriptor set schema binding names must be unique");
            }
            if (existing.binding.binding == binding.binding.binding) {
                throw std::runtime_error("descriptor set schema binding indices must be unique");
            }
        }
        bindings_.push_back(Binding{
            .name = std::string(binding.name),
            .binding = binding.binding,
        });
    }
}

DescriptorSetInfo DescriptorSetSchema::info(std::uint32_t max_sets) const {
    std::vector<DescriptorSetBindingConfig> configs;
    configs.reserve(bindings_.size());
    for (const Binding& binding : bindings_) {
        configs.push_back(binding.binding);
    }
    return DescriptorSetInfo(configs, max_sets);
}

std::uint32_t DescriptorSetSchema::binding(std::string_view name) const {
    return find(name).binding.binding;
}

DescriptorWriteBatch& DescriptorSetSchema::uniform_buffer(
    DescriptorWriteBatch& batch, VkDescriptorSet set, std::string_view name, VkBuffer buffer,
    VkDeviceSize range, VkDeviceSize offset) const {
    require_type(name, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    return batch.uniform_buffer(set, binding(name), buffer, range, offset);
}

DescriptorWriteBatch& DescriptorSetSchema::storage_buffer(
    DescriptorWriteBatch& batch, VkDescriptorSet set, std::string_view name, VkBuffer buffer,
    VkDeviceSize range, VkDeviceSize offset) const {
    require_type(name, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    return batch.storage_buffer(set, binding(name), buffer, range, offset);
}

DescriptorWriteBatch& DescriptorSetSchema::storage_image(
    DescriptorWriteBatch& batch, VkDescriptorSet set, std::string_view name,
    VkImageView image_view, VkImageLayout layout) const {
    require_type(name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    return batch.storage_image(set, binding(name), image_view, layout);
}

DescriptorWriteBatch& DescriptorSetSchema::combined_image_sampler(
    DescriptorWriteBatch& batch, VkDescriptorSet set, std::string_view name, VkSampler sampler,
    VkImageView image_view, VkImageLayout layout) const {
    require_type(name, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    return batch.combined_image_sampler(set, binding(name), sampler, image_view, layout);
}

const DescriptorSetSchema::Binding& DescriptorSetSchema::find(std::string_view name) const {
    for (const Binding& binding : bindings_) {
        if (binding.name == name) {
            return binding;
        }
    }
    throw std::runtime_error("descriptor set schema binding name is unknown");
}

void DescriptorSetSchema::require_type(std::string_view name, VkDescriptorType expected) const {
    if (find(name).binding.type != expected) {
        throw std::runtime_error("descriptor set schema binding type does not match write type");
    }
}

DescriptorSetLayout::DescriptorSetLayout(const Device& device,
                                         const VkDescriptorSetLayoutCreateInfo& info)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("descriptor set layout creation requires a valid Vulkan device");
    }

    check(vkCreateDescriptorSetLayout(device_, &info, nullptr, &layout_),
          "vkCreateDescriptorSetLayout");
}

DescriptorSetLayout::~DescriptorSetLayout() {
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
    }
}

DescriptorPool::DescriptorPool(const Device& device, const VkDescriptorPoolCreateInfo& info)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("descriptor pool creation requires a valid Vulkan device");
    }

    check(vkCreateDescriptorPool(device_, &info, nullptr, &pool_), "vkCreateDescriptorPool");
}

DescriptorPool::~DescriptorPool() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, pool_, nullptr);
    }
}

VkDescriptorSet DescriptorPool::allocate(VkDescriptorSetLayout layout) const {
    return allocate_many(layout, 1).front();
}

std::vector<VkDescriptorSet> DescriptorPool::allocate_many(VkDescriptorSetLayout layout,
                                                           std::uint32_t count) const {
    if (count == 0) {
        throw std::runtime_error("descriptor set allocation count must be positive");
    }

    std::vector<VkDescriptorSetLayout> layouts(count, layout);
    const VkDescriptorSetAllocateInfo alloc = descriptor_set_allocate_info(pool_, layouts);

    std::vector<VkDescriptorSet> sets(count, VK_NULL_HANDLE);
    check(vkAllocateDescriptorSets(device_, &alloc, sets.data()), "vkAllocateDescriptorSets");
    return sets;
}

DescriptorSetBundle::DescriptorSetBundle(const Device& device, const DescriptorSetInfo& info)
    : layout_(device, info.layout_info()), pool_(device, info.pool_info()) {
    set_ = pool_.allocate(layout_.handle());
}

DescriptorSetArray::DescriptorSetArray(const Device& device, const DescriptorSetInfo& info)
    : layout_(device, info.layout_info()), pool_(device, info.pool_info()),
      sets_(pool_.allocate_many(layout_.handle(), info.max_sets())) {}

VkDescriptorSet DescriptorSetArray::set(std::uint32_t index) const {
    if (index >= sets_.size()) {
        throw std::runtime_error("descriptor set array index is out of range");
    }
    return sets_.at(static_cast<std::size_t>(index));
}

} // namespace cubey::vulkan
