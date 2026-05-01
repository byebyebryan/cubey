#include <cubey/vulkan/shader_module.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

ShaderModule::ShaderModule(const Device& device, std::span<const std::uint32_t> code)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("shader module creation requires a valid Vulkan device");
    }
    if (code.empty()) {
        throw std::runtime_error("shader module creation requires SPIR-V code");
    }

    auto info = vk_struct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = code.size_bytes();
    info.pCode = code.data();

    check(vkCreateShaderModule(device_, &info, nullptr, &shader_module_), "vkCreateShaderModule");
}

ShaderModule::~ShaderModule() {
    if (shader_module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, shader_module_, nullptr);
    }
}

} // namespace cubey::vulkan
