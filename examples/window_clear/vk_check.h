#pragma once

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

namespace cubey::examples::window_clear {

inline void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

template <typename T> T vk_struct(VkStructureType type) {
    T value{};
    value.sType = type;
    return value;
}

} // namespace cubey::examples::window_clear
