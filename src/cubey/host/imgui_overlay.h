#pragma once

#include <cubey/host/glfw_window.h>
#include <cubey/host/ui.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <vector>

namespace cubey::host {

struct ImGuiOverlayCreateInfo {
    GlfwWindow* window = nullptr;
    cubey::vulkan::Instance* instance = nullptr;
    cubey::vulkan::Device* device = nullptr;
    cubey::vulkan::Swapchain* swapchain = nullptr;
    cubey::vulkan::FrameResources* frame_resources = nullptr;
};

class ImGuiOverlay {
  public:
    ImGuiOverlay() = default;
    ~ImGuiOverlay();

    ImGuiOverlay(const ImGuiOverlay&) = delete;
    ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;

    void create(const ImGuiOverlayCreateInfo& info);
    void destroy();

    void begin_frame();
    void discard_frame();
    [[nodiscard]] UiCaptureState capture_state() const;
    [[nodiscard]] VkCommandBuffer record(cubey::render::FrameSlot frame_slot,
                                         cubey::render::ColorTargetView color_target);

    [[nodiscard]] bool active() const {
        return active_;
    }

  private:
    void create_descriptor_pool();
    void allocate_command_buffers(std::uint32_t frame_slot_count);

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfoKHR pipeline_rendering_info_{};
    std::vector<VkCommandBuffer> command_buffers_;
    bool active_ = false;
    bool frame_started_ = false;
};

} // namespace cubey::host
