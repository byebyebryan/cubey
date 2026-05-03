#include <cubey/vulkan/command_pool.h>

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <type_traits>

void test_command_pool_exposes_command_buffer_ownership() {
    cubey::vulkan::CommandPoolConfig config;
    if (config.flags != 0) {
        throw std::runtime_error("command pool flags should default to zero");
    }

    static_assert(std::is_constructible_v<cubey::vulkan::CommandPool, const cubey::vulkan::Device&,
                                          const cubey::vulkan::CommandPoolConfig&>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::CommandPool::handle),
                                 VkCommandPool (cubey::vulkan::CommandPool::*)() const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::CommandPool::allocate_primary),
                                 VkCommandBuffer (cubey::vulkan::CommandPool::*)() const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::begin_command_buffer),
                                 void (*)(VkCommandBuffer, VkCommandBufferUsageFlags)>);
}
