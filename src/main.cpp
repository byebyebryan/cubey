#include "config.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct Options {
    bool headless = false;
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t frames = 0;
    std::string output = "cubey-vulkan.ppm";
};

struct PushConstants {
    uint32_t width;
    uint32_t height;
    float time;
    uint32_t bgra;
};

static void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(result));
    }
}

template <typename T>
static T vk_struct(VkStructureType type) {
    T value{};
    value.sType = type;
    return value;
}

static std::vector<uint32_t> read_spirv(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error(std::string("failed to open shader: ") + path);
    }

    std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        throw std::runtime_error("SPIR-V shader has invalid byte size");
    }

    std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
        throw std::runtime_error("failed to read SPIR-V shader");
    }
    return code;
}

static Options parse_args(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        auto need_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--headless") {
            opts.headless = true;
        } else if (arg == "--output") {
            opts.output = need_value("--output");
        } else if (arg == "--width") {
            opts.width = static_cast<uint32_t>(std::strtoul(need_value("--width"), nullptr, 10));
        } else if (arg == "--height") {
            opts.height = static_cast<uint32_t>(std::strtoul(need_value("--height"), nullptr, 10));
        } else if (arg == "--frames") {
            opts.frames = static_cast<uint32_t>(std::strtoul(need_value("--frames"), nullptr, 10));
        } else {
            throw std::runtime_error(std::string("unknown argument: ") + std::string(arg));
        }
    }
    if (opts.width == 0 || opts.height == 0) {
        throw std::runtime_error("width and height must be positive");
    }
    if (opts.headless && opts.frames == 0) {
        opts.frames = 1;
    }
    return opts;
}

class VulkanComputeSpike {
public:
    explicit VulkanComputeSpike(const Options& opts) : opts_(opts) {}

    ~VulkanComputeSpike() {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
        }
        if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        if (descriptor_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
        if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        if (descriptor_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
        if (command_pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, command_pool_, nullptr);
        if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer_, nullptr);
        if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
        if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
        if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
        if (window_ != nullptr) glfwDestroyWindow(window_);
        if (glfw_initialized_) glfwTerminate();
    }

    void run() {
        if (!opts_.headless) {
            init_window();
        }
        create_instance();
        if (!opts_.headless) {
            create_surface();
        }
        select_physical_device();
        create_device();
        if (!opts_.headless) {
            create_swapchain();
        }
        create_buffer();
        create_pipeline();
        create_descriptors();
        create_commands();
        if (opts_.headless) {
            dispatch_headless();
            pixels_ = read_pixels();
            verify_pixels();
            write_ppm();
        } else {
            render_window();
        }
    }

    const char* device_name() const {
        return device_properties_.deviceName;
    }

private:
    void init_window() {
        if (!glfwInit()) {
            throw std::runtime_error("glfwInit failed");
        }
        glfw_initialized_ = true;

        if (!glfwVulkanSupported()) {
            throw std::runtime_error("GLFW reports Vulkan is not supported");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ = glfwCreateWindow(static_cast<int>(opts_.width), static_cast<int>(opts_.height), "cubey vulkan", nullptr, nullptr);
        if (window_ == nullptr) {
            throw std::runtime_error("glfwCreateWindow failed");
        }
    }

    void create_instance() {
        auto app = vk_struct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName = "cubey vulkan spike";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "cubey";
        app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_2;

        auto info = vk_struct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        info.pApplicationInfo = &app;
        std::vector<const char*> extensions;
        if (!opts_.headless) {
            uint32_t extension_count = 0;
            const char** required = glfwGetRequiredInstanceExtensions(&extension_count);
            if (required == nullptr || extension_count == 0) {
                throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
            }
            extensions.assign(required, required + extension_count);
            info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            info.ppEnabledExtensionNames = extensions.data();
        }
        check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
    }

    void create_surface() {
        check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_), "glfwCreateWindowSurface");
    }

    void select_physical_device() {
        uint32_t device_count = 0;
        check(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr), "vkEnumeratePhysicalDevices count");
        if (device_count == 0) {
            throw std::runtime_error("no Vulkan physical devices found");
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        check(vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()), "vkEnumeratePhysicalDevices");

        for (VkPhysicalDevice candidate : devices) {
            uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());

            for (uint32_t i = 0; i < family_count; ++i) {
                VkBool32 present_supported = VK_TRUE;
                if (!opts_.headless) {
                    check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present_supported), "vkGetPhysicalDeviceSurfaceSupportKHR");
                }
                if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 && present_supported == VK_TRUE) {
                    physical_device_ = candidate;
                    queue_family_ = i;
                    vkGetPhysicalDeviceProperties(physical_device_, &device_properties_);
                    return;
                }
            }
        }

        throw std::runtime_error(opts_.headless
            ? "no Vulkan device with a compute queue found"
            : "no Vulkan device with one queue family supporting compute and present found");
    }

    void create_device() {
        float priority = 1.0f;
        auto queue_info = vk_struct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queue_info.queueFamilyIndex = queue_family_;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;

        auto info = vk_struct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue_info;
        std::array<const char*, 1> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (!opts_.headless) {
            info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            info.ppEnabledExtensionNames = extensions.data();
        }
        check(vkCreateDevice(physical_device_, &info, nullptr, &device_), "vkCreateDevice");
        vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    }

    static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR flags) {
        constexpr VkCompositeAlphaFlagBitsKHR choices[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (VkCompositeAlphaFlagBitsKHR choice : choices) {
            if ((flags & choice) != 0) {
                return choice;
            }
        }
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    static bool is_bgra(VkFormat format) {
        return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    }

    void create_swapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            throw std::runtime_error("surface does not support transfer-dst swapchain images");
        }

        uint32_t format_count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR count");
        if (format_count == 0) {
            throw std::runtime_error("surface reported no formats");
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");

        surface_format_ = formats[0];
        for (const VkSurfaceFormatKHR& format : formats) {
            if (format.format == VK_FORMAT_R8G8B8A8_UNORM || format.format == VK_FORMAT_B8G8R8A8_UNORM) {
                surface_format_ = format;
                break;
            }
        }

        if (caps.currentExtent.width != UINT32_MAX) {
            swapchain_extent_ = caps.currentExtent;
        } else {
            int fb_w = 0;
            int fb_h = 0;
            glfwGetFramebufferSize(window_, &fb_w, &fb_h);
            swapchain_extent_.width = std::clamp(static_cast<uint32_t>(std::max(fb_w, 1)), caps.minImageExtent.width, caps.maxImageExtent.width);
            swapchain_extent_.height = std::clamp(static_cast<uint32_t>(std::max(fb_h, 1)), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        opts_.width = swapchain_extent_.width;
        opts_.height = swapchain_extent_.height;

        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) {
            image_count = std::min(image_count, caps.maxImageCount);
        }

        auto info = vk_struct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        info.surface = surface_;
        info.minImageCount = image_count;
        info.imageFormat = surface_format_.format;
        info.imageColorSpace = surface_format_.colorSpace;
        info.imageExtent = swapchain_extent_;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha);
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;

        check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");

        uint32_t actual_count = 0;
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, nullptr), "vkGetSwapchainImagesKHR count");
        swapchain_images_.resize(actual_count);
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, swapchain_images_.data()), "vkGetSwapchainImagesKHR");
        swapchain_layouts_.assign(actual_count, VK_IMAGE_LAYOUT_UNDEFINED);
    }

    uint32_t find_memory_type(uint32_t bits, VkMemoryPropertyFlags required) const {
        VkPhysicalDeviceMemoryProperties props{};
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &props);
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            bool compatible = (bits & (1u << i)) != 0;
            bool has_flags = (props.memoryTypes[i].propertyFlags & required) == required;
            if (compatible && has_flags) {
                return i;
            }
        }
        throw std::runtime_error("no compatible host-visible coherent Vulkan memory type found");
    }

    void create_buffer() {
        buffer_size_ = static_cast<VkDeviceSize>(opts_.width) * opts_.height * sizeof(uint32_t);

        auto buffer_info = vk_struct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        buffer_info.size = buffer_size_;
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &buffer_info, nullptr, &buffer_), "vkCreateBuffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, buffer_, &req);

        auto alloc = vk_struct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = find_memory_type(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        check(vkAllocateMemory(device_, &alloc, nullptr, &memory_), "vkAllocateMemory");
        check(vkBindBufferMemory(device_, buffer_, memory_, 0), "vkBindBufferMemory");

        void* mapped = nullptr;
        check(vkMapMemory(device_, memory_, 0, buffer_size_, 0, &mapped), "vkMapMemory init");
        std::memset(mapped, 0, static_cast<size_t>(buffer_size_));
        vkUnmapMemory(device_, memory_);
    }

    void create_pipeline() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        auto layout_info = vk_struct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &descriptor_set_layout_), "vkCreateDescriptorSetLayout");

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push.offset = 0;
        push.size = sizeof(PushConstants);

        auto pipeline_layout_info = vk_struct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &descriptor_set_layout_;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout_), "vkCreatePipelineLayout");

        auto code = read_spirv(CUBEY_COMP_SHADER_PATH);
        auto shader_info = vk_struct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        shader_info.codeSize = code.size() * sizeof(uint32_t);
        shader_info.pCode = code.data();
        VkShaderModule shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device_, &shader_info, nullptr, &shader), "vkCreateShaderModule");

        auto stage = vk_struct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shader;
        stage.pName = "main";

        auto pipeline_info = vk_struct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
        pipeline_info.stage = stage;
        pipeline_info.layout = pipeline_layout_;
        VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_);
        vkDestroyShaderModule(device_, shader, nullptr);
        check(result, "vkCreateComputePipelines");
    }

    void create_descriptors() {
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_size.descriptorCount = 1;

        auto pool_info = vk_struct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_), "vkCreateDescriptorPool");

        auto alloc = vk_struct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        alloc.descriptorPool = descriptor_pool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &descriptor_set_layout_;
        check(vkAllocateDescriptorSets(device_, &alloc, &descriptor_set_), "vkAllocateDescriptorSets");

        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = buffer_;
        buffer_info.offset = 0;
        buffer_info.range = buffer_size_;

        auto write = vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = descriptor_set_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer_info;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    void create_commands() {
        auto pool_info = vk_struct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_;
        check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool");
    }

    VkCommandBuffer allocate_command_buffer() {
        auto alloc = vk_struct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        alloc.commandPool = command_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device_, &alloc, &cmd), "vkAllocateCommandBuffers");
        return cmd;
    }

    void dispatch_headless() {
        VkCommandBuffer cmd = allocate_command_buffer();

        auto begin = vk_struct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");

        PushConstants push{opts_.width, opts_.height, static_cast<float>(opts_.frames) / 60.0f, 0};
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1, &descriptor_set_, 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &push);
        vkCmdDispatch(cmd, (opts_.width + 15) / 16, (opts_.height + 15) / 16, 1);

        auto barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0,
            1,
            &barrier,
            0,
            nullptr,
            0,
            nullptr
        );

        check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

        auto fence_info = vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        VkFence fence = VK_NULL_HANDLE;
        check(vkCreateFence(device_, &fence_info, nullptr, &fence), "vkCreateFence");

        auto submit = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        VkResult submit_result = vkQueueSubmit(queue_, 1, &submit, fence);
        if (submit_result == VK_SUCCESS) {
            submit_result = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
        }
        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
        check(submit_result, "compute submit/wait");
    }

    void record_compute_to_swapchain(VkCommandBuffer cmd, uint32_t image_index, float time) {
        PushConstants push{opts_.width, opts_.height, time, is_bgra(surface_format_.format) ? 1u : 0u};
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1, &descriptor_set_, 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &push);
        vkCmdDispatch(cmd, (opts_.width + 15) / 16, (opts_.height + 15) / 16, 1);

        auto buffer_barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            1,
            &buffer_barrier,
            0,
            nullptr,
            0,
            nullptr
        );

        auto to_transfer = vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        to_transfer.oldLayout = swapchain_layouts_[image_index];
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = swapchain_images_[image_index];
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.baseMipLevel = 0;
        to_transfer.subresourceRange.levelCount = 1;
        to_transfer.subresourceRange.baseArrayLayer = 0;
        to_transfer.subresourceRange.layerCount = 1;
        to_transfer.srcAccessMask = 0;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &to_transfer
        );

        VkBufferImageCopy copy{};
        copy.bufferOffset = 0;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset = {0, 0, 0};
        copy.imageExtent = {opts_.width, opts_.height, 1};
        vkCmdCopyBufferToImage(
            cmd,
            buffer_,
            swapchain_images_[image_index],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy
        );

        auto to_present = vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.image = swapchain_images_[image_index];
        to_present.subresourceRange = to_transfer.subresourceRange;
        to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_present.dstAccessMask = 0;
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &to_present
        );
        swapchain_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    void render_one_window_frame(uint32_t frame_index) {
        auto semaphore_info = vk_struct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkSemaphore present_ready = VK_NULL_HANDLE;
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available), "vkCreateSemaphore image_available");
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &present_ready), "vkCreateSemaphore present_ready");

        auto fence_info = vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        VkFence fence = VK_NULL_HANDLE;
        check(vkCreateFence(device_, &fence_info, nullptr, &fence), "vkCreateFence window");

        uint32_t image_index = 0;
        VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available, VK_NULL_HANDLE, &image_index);
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            vkDestroyFence(device_, fence, nullptr);
            vkDestroySemaphore(device_, present_ready, nullptr);
            vkDestroySemaphore(device_, image_available, nullptr);
            check(acquired, "vkAcquireNextImageKHR");
        }

        VkCommandBuffer cmd = allocate_command_buffer();
        auto begin = vk_struct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer window");
        record_compute_to_swapchain(cmd, image_index, static_cast<float>(frame_index) / 60.0f);
        check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer window");

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        auto submit = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &present_ready;
        VkResult submit_result = vkQueueSubmit(queue_, 1, &submit, fence);
        if (submit_result == VK_SUCCESS) {
            submit_result = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
        }
        check(submit_result, "window submit/wait");

        auto present = vk_struct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &present_ready;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index;
        VkResult present_result = vkQueuePresentKHR(queue_, &present);
        if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
            check(present_result, "vkQueuePresentKHR");
        }

        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
        vkDestroyFence(device_, fence, nullptr);
        vkDestroySemaphore(device_, present_ready, nullptr);
        vkDestroySemaphore(device_, image_available, nullptr);
    }

    void render_window() {
        std::printf(
            "window mode: %s presenting %ux%u Vulkan swapchain images\n",
            device_name(),
            opts_.width,
            opts_.height
        );

        uint32_t frame = 0;
        while (!glfwWindowShouldClose(window_) && (opts_.frames == 0 || frame < opts_.frames)) {
            glfwPollEvents();
            render_one_window_frame(frame);
            ++frame;
        }
        vkDeviceWaitIdle(device_);
    }

    std::vector<uint32_t> read_pixels() {
        std::vector<uint32_t> pixels(static_cast<size_t>(opts_.width) * opts_.height);
        void* mapped = nullptr;
        check(vkMapMemory(device_, memory_, 0, buffer_size_, 0, &mapped), "vkMapMemory readback");
        std::memcpy(pixels.data(), mapped, static_cast<size_t>(buffer_size_));
        vkUnmapMemory(device_, memory_);
        return pixels;
    }

    void verify_pixels() const {
        if (pixels_.empty()) {
            throw std::runtime_error("pixel buffer is empty");
        }

        uint32_t first = pixels_.front();
        bool varied = false;
        bool alpha_ok = true;
        for (uint32_t px : pixels_) {
            varied = varied || px != first;
            alpha_ok = alpha_ok && ((px >> 24) == 255u);
        }

        if (!varied) {
            throw std::runtime_error("compute output did not vary across the image");
        }
        if (!alpha_ok) {
            throw std::runtime_error("compute output alpha channel verification failed");
        }
    }

    void write_ppm() const {
        std::ofstream out(opts_.output, std::ios::binary);
        if (!out) {
            throw std::runtime_error("failed to open output image: " + opts_.output);
        }
        out << "P6\n" << opts_.width << " " << opts_.height << "\n255\n";
        for (uint32_t px : pixels_) {
            unsigned char rgb[3] = {
                static_cast<unsigned char>(px & 0xffu),
                static_cast<unsigned char>((px >> 8) & 0xffu),
                static_cast<unsigned char>((px >> 16) & 0xffu),
            };
            out.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
        }
    }

    Options opts_;
    bool glfw_initialized_ = false;
    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties device_properties_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format_{};
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageLayout> swapchain_layouts_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize buffer_size_ = 0;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<uint32_t> pixels_;
};

int main(int argc, char** argv) {
    try {
        Options opts = parse_args(argc, argv);
        VulkanComputeSpike spike(opts);
        spike.run();
        if (opts.headless) {
            std::printf(
                "verification passed: %s wrote %ux%u Vulkan compute image to %s\n",
                spike.device_name(),
                opts.width,
                opts.height,
                opts.output.c_str()
            );
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cubey: %s\n", e.what());
        return 1;
    }
}
