#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cubey::vulkan {

struct InstanceConfig {
    std::string application_name = "cubey";
    std::uint32_t application_version = 0;
    std::vector<const char*> required_extensions;
    bool validation = true;
    bool require_validation = false;
};

class Instance {
  public:
    explicit Instance(const InstanceConfig& config);
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    VkInstance handle() const {
        return instance_;
    }
    bool validation_enabled() const {
        return validation_enabled_;
    }
    bool debug_utils_enabled() const {
        return debug_utils_enabled_;
    }

  private:
    void configure_validation(const InstanceConfig& config);
    void create_instance(const InstanceConfig& config);
    void create_debug_messenger(bool require_validation);
    void destroy_debug_messenger();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    bool validation_enabled_ = false;
    bool debug_utils_enabled_ = false;
};

} // namespace cubey::vulkan
