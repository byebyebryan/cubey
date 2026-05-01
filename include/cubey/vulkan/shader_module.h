#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace cubey::vulkan {

class ShaderModule {
  public:
    ShaderModule(const Device& device, std::span<const std::uint32_t> code);
    ~ShaderModule();

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    VkShaderModule handle() const {
        return shader_module_;
    }

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule shader_module_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
