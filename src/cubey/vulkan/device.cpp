#include <cubey/vulkan/device.h>

#include <cubey/vulkan/vk_check.h>

#include <array>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cubey::vulkan {
namespace {

constexpr std::uint32_t make_vk_api_version(std::uint32_t variant, std::uint32_t major,
                                            std::uint32_t minor, std::uint32_t patch) {
    return (variant << 29U) | (major << 22U) | (minor << 12U) | patch;
}

bool device_supports_swapchain(VkPhysicalDevice device) {
    std::uint32_t count = 0;
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
          "vkEnumerateDeviceExtensionProperties count");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
          "vkEnumerateDeviceExtensionProperties");

    for (const VkExtensionProperties& extension : extensions) {
        if (std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
            return true;
        }
    }
    return false;
}

bool device_supports_dynamic_rendering(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < make_vk_api_version(0, 1, 3, 0)) {
        return false;
    }

    auto dynamic_rendering = vk_struct<VkPhysicalDeviceDynamicRenderingFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES);
    auto features =
        vk_struct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    features.pNext = &dynamic_rendering;
    vkGetPhysicalDeviceFeatures2(device, &features);
    return dynamic_rendering.dynamicRendering == VK_TRUE;
}

} // namespace

Device::Device(const Instance& instance, const DeviceConfig& config) {
    select_physical_device(instance, config);
    create_device(config);
}

Device::~Device() {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::uint32_t Device::find_memory_type(std::uint32_t type_bits,
                                       VkMemoryPropertyFlags required) const {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);

    for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        const bool type_matches = (type_bits & (1U << i)) != 0;
        const bool flags_match =
            (memory_properties.memoryTypes[i].propertyFlags & required) == required;
        if (type_matches && flags_match) {
            return i;
        }
    }

    throw std::runtime_error("no compatible Vulkan memory type found");
}

void Device::wait_idle() const {
    if (device_ != VK_NULL_HANDLE) {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

void Device::select_physical_device(const Instance& instance, const DeviceConfig& config) {
    if (config.require_present && config.surface == VK_NULL_HANDLE) {
        throw std::runtime_error("present-capable device selection requires a Vulkan surface");
    }

    std::uint32_t device_count = 0;
    check(vkEnumeratePhysicalDevices(instance.handle(), &device_count, nullptr),
          "vkEnumeratePhysicalDevices count");
    if (device_count == 0) {
        throw std::runtime_error("no Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    check(vkEnumeratePhysicalDevices(instance.handle(), &device_count, devices.data()),
          "vkEnumeratePhysicalDevices");

    for (VkPhysicalDevice candidate : devices) {
        if (config.require_present && !device_supports_swapchain(candidate)) {
            continue;
        }
        if (config.require_dynamic_rendering && !device_supports_dynamic_rendering(candidate)) {
            continue;
        }

        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());

        for (std::uint32_t i = 0; i < family_count; ++i) {
            VkBool32 present_supported = VK_TRUE;
            if (config.require_present) {
                check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, config.surface,
                                                           &present_supported),
                      "vkGetPhysicalDeviceSurfaceSupportKHR");
            }

            if ((families[i].queueFlags & config.required_queue_flags) ==
                    config.required_queue_flags &&
                present_supported == VK_TRUE) {
                physical_device_ = candidate;
                queue_family_ = i;
                vkGetPhysicalDeviceProperties(physical_device_, &properties_);
                return;
            }
        }
    }

    if (config.require_present) {
        throw std::runtime_error(
            "no Vulkan device with one queue family supporting required queues and present found; "
            "Cubey currently requires one family for requested graphics/compute/present work");
    }
    if (config.require_dynamic_rendering) {
        throw std::runtime_error(
            "no Vulkan device with required queues and dynamic rendering found");
    }
    throw std::runtime_error("no Vulkan device with required queues found");
}

void Device::create_device(const DeviceConfig& config) {
    float priority = 1.0f;
    auto queue_info =
        vk_struct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queue_info.queueFamilyIndex = queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    std::array<const char*, 1> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    auto info = vk_struct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;

    auto dynamic_rendering = vk_struct<VkPhysicalDeviceDynamicRenderingFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES);
    if (config.require_dynamic_rendering) {
        dynamic_rendering.dynamicRendering = VK_TRUE;
        info.pNext = &dynamic_rendering;
    }

    if (config.require_present) {
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
    }

    check(vkCreateDevice(physical_device_, &info, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
}

} // namespace cubey::vulkan
