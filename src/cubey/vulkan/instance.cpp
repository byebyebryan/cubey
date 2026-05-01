#include <cubey/vulkan/instance.h>

#include <cubey/vulkan/vk_check.h>

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::vulkan {
namespace {

constexpr std::uint32_t make_vk_version(std::uint32_t major, std::uint32_t minor,
                                        std::uint32_t patch) {
    return (major << 22U) | (minor << 12U) | patch;
}

constexpr std::uint32_t make_vk_api_version(std::uint32_t variant, std::uint32_t major,
                                            std::uint32_t minor, std::uint32_t patch) {
    return (variant << 29U) | (major << 22U) | (minor << 12U) | patch;
}

constexpr std::array<const char*, 1> validation_layers() {
    return {"VK_LAYER_KHRONOS_validation"};
}

bool instance_layer_available(const char* name) {
    std::uint32_t count = 0;
    check(vkEnumerateInstanceLayerProperties(&count, nullptr),
          "vkEnumerateInstanceLayerProperties count");
    std::vector<VkLayerProperties> layers(count);
    check(vkEnumerateInstanceLayerProperties(&count, layers.data()),
          "vkEnumerateInstanceLayerProperties");

    for (const VkLayerProperties& layer : layers) {
        if (std::string_view(layer.layerName) == name) {
            return true;
        }
    }
    return false;
}

bool instance_extension_available(const char* name) {
    std::uint32_t count = 0;
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
          "vkEnumerateInstanceExtensionProperties count");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
          "vkEnumerateInstanceExtensionProperties");

    for (const VkExtensionProperties& extension : extensions) {
        if (std::string_view(extension.extensionName) == name) {
            return true;
        }
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT unused_type,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* unused_user_data) {
    (void)unused_type;
    (void)unused_user_data;
    const char* label =
        severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning";
    const char* message =
        data != nullptr && data->pMessage != nullptr ? data->pMessage : "(no message)";
    std::fprintf(stderr, "vulkan validation %s: %s\n", label, message);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info() {
    auto info = vk_struct<VkDebugUtilsMessengerCreateInfoEXT>(
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debug_callback;
    return info;
}

} // namespace

Instance::Instance(const InstanceConfig& config) {
    try {
        configure_validation(config);
        create_instance(config);
        create_debug_messenger(config.require_validation);
    } catch (...) {
        destroy_debug_messenger();
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
        throw;
    }
}

Instance::~Instance() {
    destroy_debug_messenger();
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

void Instance::configure_validation(const InstanceConfig& config) {
    if (!config.validation) {
        return;
    }

    for (const char* layer : validation_layers()) {
        if (!instance_layer_available(layer)) {
            if (config.require_validation) {
                throw std::runtime_error(std::string("required validation layer is unavailable: ") +
                                         layer);
            }
            std::fprintf(stderr, "cubey: validation layer unavailable, continuing without it: %s\n",
                         layer);
            return;
        }
    }

    validation_enabled_ = true;
    if (instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        debug_utils_enabled_ = true;
    } else {
        std::fprintf(
            stderr,
            "cubey: debug utils extension unavailable; validation messages may not be routed\n");
    }
}

void Instance::create_instance(const InstanceConfig& config) {
    auto app = vk_struct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = config.application_name.c_str();
    app.applicationVersion = config.application_version;
    app.pEngineName = "cubey";
    app.engineVersion = make_vk_version(0, 1, 0);
    app.apiVersion = make_vk_api_version(0, 1, 2, 0);

    std::vector<const char*> extensions = config.required_extensions;
    if (debug_utils_enabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    auto info = vk_struct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    const auto layers = validation_layers();
    if (validation_enabled_) {
        info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_info{};
    if (debug_utils_enabled_) {
        debug_info = debug_messenger_info();
        info.pNext = &debug_info;
    }

    check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
}

void Instance::create_debug_messenger(bool require_validation) {
    if (!debug_utils_enabled_) {
        return;
    }

    auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (create_fn == nullptr) {
        if (require_validation) {
            throw std::runtime_error("vkCreateDebugUtilsMessengerEXT is unavailable");
        }
        std::fprintf(
            stderr,
            "cubey: vkCreateDebugUtilsMessengerEXT unavailable; validation messages may not be "
            "routed\n");
        debug_utils_enabled_ = false;
        return;
    }

    auto info = debug_messenger_info();
    check(create_fn(instance_, &info, nullptr, &debug_messenger_),
          "vkCreateDebugUtilsMessengerEXT");
}

void Instance::destroy_debug_messenger() {
    if (debug_messenger_ == VK_NULL_HANDLE) {
        return;
    }

    auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_fn != nullptr) {
        destroy_fn(instance_, debug_messenger_, nullptr);
    }
    debug_messenger_ = VK_NULL_HANDLE;
}

} // namespace cubey::vulkan
