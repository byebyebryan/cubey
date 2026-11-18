#pragma once

#include <cubey/vulkan/device.h>

#include <string>
#include <vector>

namespace cubey::host {

struct VulkanRequirements {
    std::vector<std::string> instance_extensions;
    vulkan::DeviceRequirements device{};
};

} // namespace cubey::host
