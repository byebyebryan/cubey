#include <cubey/render/material.h>

#include <algorithm>
#include <stdexcept>

namespace cubey::render {
namespace {

void validate_descriptor_set(const MaterialDescriptorSetLayout& descriptor_set) {
    for (const cubey::vulkan::DescriptorSetBindingConfig& binding : descriptor_set.bindings) {
        if (binding.descriptor_count == 0) {
            throw std::runtime_error("material pass descriptor binding count must be nonzero");
        }
    }

    for (auto current = descriptor_set.bindings.begin();
         current != descriptor_set.bindings.end(); ++current) {
        const auto duplicate =
            std::find_if(descriptor_set.bindings.begin(), current,
                         [binding = current->binding](
                             const cubey::vulkan::DescriptorSetBindingConfig& prior) {
                             return prior.binding == binding;
                         });
        if (duplicate != current) {
            throw std::runtime_error("material pass descriptor set has duplicate bindings");
        }
    }
}

} // namespace

void validate_material_pass_info(const MaterialPassInfo& info) {
    if (info.label.empty()) {
        throw std::runtime_error("material pass label must be non-empty");
    }

    for (auto current = info.descriptor_sets.begin();
         current != info.descriptor_sets.end(); ++current) {
        validate_descriptor_set(*current);
        const auto duplicate =
            std::find_if(info.descriptor_sets.begin(), current,
                         [set = current->set](const MaterialDescriptorSetLayout& prior) {
                             return prior.set == set;
                         });
        if (duplicate != current) {
            throw std::runtime_error("material pass descriptor sets must be unique");
        }
    }

    for (const VkPushConstantRange& push_constant : info.push_constants) {
        if (push_constant.size == 0) {
            throw std::runtime_error("material pass push constant size must be nonzero");
        }
    }
}

const MaterialDescriptorSetLayout& material_descriptor_set_layout(const MaterialPassInfo& info,
                                                                  std::uint32_t set) {
    validate_material_pass_info(info);
    const auto descriptor_set =
        std::find_if(info.descriptor_sets.begin(), info.descriptor_sets.end(),
                     [set](const MaterialDescriptorSetLayout& candidate) {
                         return candidate.set == set;
                     });
    if (descriptor_set == info.descriptor_sets.end()) {
        throw std::runtime_error("material pass descriptor set is not declared");
    }
    return *descriptor_set;
}

cubey::vulkan::DescriptorSetInfo material_descriptor_set_info(const MaterialPassInfo& info,
                                                              std::uint32_t set,
                                                              std::uint32_t max_sets) {
    const MaterialDescriptorSetLayout& descriptor_set = material_descriptor_set_layout(info, set);
    return cubey::vulkan::DescriptorSetInfo(descriptor_set.bindings, max_sets);
}

void apply_material_pass_state(const MaterialPassInfo& info,
                               cubey::vulkan::DynamicGraphicsPipelineConfig& config) {
    validate_material_pass_info(info);
    config.topology = info.topology;
    config.polygon_mode = info.polygon_mode;
    config.cull_mode = info.cull_mode;
    config.front_face = info.front_face;
    config.depth_test = info.depth_test;
    config.depth_write = info.depth_write;
    config.depth_compare_op = info.depth_compare_op;
    config.blend_enable = info.blend_enable;
    config.src_color_blend_factor = info.src_color_blend_factor;
    config.dst_color_blend_factor = info.dst_color_blend_factor;
    config.color_blend_op = info.color_blend_op;
    config.src_alpha_blend_factor = info.src_alpha_blend_factor;
    config.dst_alpha_blend_factor = info.dst_alpha_blend_factor;
    config.alpha_blend_op = info.alpha_blend_op;
}

} // namespace cubey::render
