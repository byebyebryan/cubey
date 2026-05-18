#include "imgui_overlay.h"

#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/vk_check.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <array>
#include <cstddef>
#include <stdexcept>

namespace cubey::host {
namespace {

void check_imgui_vk_result(VkResult result) {
    cubey::vulkan::check(result, "Dear ImGui Vulkan backend");
}

void validate_create_info(const ImGuiOverlayCreateInfo& info) {
    if (info.window == nullptr) {
        throw std::runtime_error("ImGui overlay requires a window");
    }
    if (info.instance == nullptr) {
        throw std::runtime_error("ImGui overlay requires a Vulkan instance");
    }
    if (info.device == nullptr) {
        throw std::runtime_error("ImGui overlay requires a Vulkan device");
    }
    if (info.swapchain == nullptr) {
        throw std::runtime_error("ImGui overlay requires a swapchain");
    }
    if (info.frame_resources == nullptr) {
        throw std::runtime_error("ImGui overlay requires frame resources");
    }
}

[[nodiscard]] constexpr ImVec4 color(float red, float green, float blue,
                                     float alpha = 1.0F) noexcept {
    return ImVec4(red, green, blue, alpha);
}

void apply_cubey_debug_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14.0F, 12.0F);
    style.FramePadding = ImVec2(9.0F, 5.0F);
    style.CellPadding = ImVec2(8.0F, 5.0F);
    style.ItemSpacing = ImVec2(10.0F, 7.0F);
    style.ItemInnerSpacing = ImVec2(8.0F, 5.0F);
    style.TouchExtraPadding = ImVec2(0.0F, 0.0F);
    style.IndentSpacing = 18.0F;
    style.ScrollbarSize = 12.0F;
    style.GrabMinSize = 10.0F;
    style.WindowMinSize = ImVec2(360.0F, 80.0F);

    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.TabBorderSize = 0.0F;

    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 0.0F;
    style.PopupRounding = 0.0F;
    style.ScrollbarRounding = 0.0F;
    style.GrabRounding = 0.0F;
    style.TabRounding = 0.0F;

    style.WindowTitleAlign = ImVec2(0.0F, 0.5F);
    style.ColorButtonPosition = ImGuiDir_Right;
    style.SeparatorTextBorderSize = 1.0F;
    style.SeparatorTextAlign = ImVec2(0.0F, 0.5F);
    style.SeparatorTextPadding = ImVec2(0.0F, 8.0F);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = color(0.88F, 0.90F, 0.91F);
    colors[ImGuiCol_TextDisabled] = color(0.46F, 0.49F, 0.51F);
    colors[ImGuiCol_WindowBg] = color(0.0F, 0.0F, 0.0F, 0.50F);
    colors[ImGuiCol_ChildBg] = color(0.0F, 0.0F, 0.0F, 0.35F);
    colors[ImGuiCol_PopupBg] = color(0.010F, 0.014F, 0.020F, 0.92F);
    colors[ImGuiCol_Border] = color(0.20F, 0.28F, 0.34F, 0.70F);
    colors[ImGuiCol_BorderShadow] = color(0.0F, 0.0F, 0.0F, 0.0F);
    colors[ImGuiCol_FrameBg] = color(0.016F, 0.024F, 0.032F, 0.84F);
    colors[ImGuiCol_FrameBgHovered] = color(0.040F, 0.070F, 0.090F, 0.90F);
    colors[ImGuiCol_FrameBgActive] = color(0.055F, 0.100F, 0.130F, 0.95F);
    colors[ImGuiCol_TitleBg] = color(0.0F, 0.0F, 0.0F, 0.72F);
    colors[ImGuiCol_TitleBgActive] = color(0.010F, 0.024F, 0.034F, 0.82F);
    colors[ImGuiCol_TitleBgCollapsed] = color(0.0F, 0.0F, 0.0F, 0.50F);
    colors[ImGuiCol_MenuBarBg] = color(0.012F, 0.022F, 0.032F, 0.84F);
    colors[ImGuiCol_ScrollbarBg] = color(0.0F, 0.0F, 0.0F, 0.20F);
    colors[ImGuiCol_ScrollbarGrab] = color(0.17F, 0.23F, 0.28F, 0.82F);
    colors[ImGuiCol_ScrollbarGrabHovered] = color(0.25F, 0.35F, 0.42F, 0.90F);
    colors[ImGuiCol_ScrollbarGrabActive] = color(0.34F, 0.48F, 0.56F, 1.0F);
    colors[ImGuiCol_CheckMark] = color(0.60F, 0.80F, 0.88F, 1.0F);
    colors[ImGuiCol_SliderGrab] = color(0.42F, 0.66F, 0.76F, 1.0F);
    colors[ImGuiCol_SliderGrabActive] = color(0.62F, 0.84F, 0.92F, 1.0F);
    colors[ImGuiCol_Button] = color(0.030F, 0.048F, 0.060F, 0.88F);
    colors[ImGuiCol_ButtonHovered] = color(0.055F, 0.090F, 0.115F, 0.94F);
    colors[ImGuiCol_ButtonActive] = color(0.075F, 0.125F, 0.160F, 1.0F);
    colors[ImGuiCol_Header] = color(0.026F, 0.055F, 0.075F, 0.86F);
    colors[ImGuiCol_HeaderHovered] = color(0.050F, 0.105F, 0.135F, 0.92F);
    colors[ImGuiCol_HeaderActive] = color(0.070F, 0.150F, 0.190F, 0.98F);
    colors[ImGuiCol_Separator] = color(0.19F, 0.26F, 0.31F, 0.65F);
    colors[ImGuiCol_SeparatorHovered] = color(0.34F, 0.52F, 0.62F, 0.95F);
    colors[ImGuiCol_SeparatorActive] = color(0.54F, 0.74F, 0.84F, 1.0F);
    colors[ImGuiCol_ResizeGrip] = color(0.20F, 0.30F, 0.36F, 0.25F);
    colors[ImGuiCol_ResizeGripHovered] = color(0.40F, 0.62F, 0.74F, 0.50F);
    colors[ImGuiCol_ResizeGripActive] = color(0.62F, 0.84F, 0.92F, 0.80F);
    colors[ImGuiCol_Tab] = color(0.018F, 0.040F, 0.055F, 0.90F);
    colors[ImGuiCol_TabHovered] = color(0.055F, 0.120F, 0.155F, 0.96F);
    colors[ImGuiCol_TabSelected] = color(0.035F, 0.085F, 0.110F, 0.96F);
    colors[ImGuiCol_TabSelectedOverline] = color(0.52F, 0.74F, 0.84F, 1.0F);
    colors[ImGuiCol_TabDimmed] = color(0.010F, 0.020F, 0.028F, 0.85F);
    colors[ImGuiCol_TabDimmedSelected] = color(0.022F, 0.055F, 0.072F, 0.92F);
    colors[ImGuiCol_TabDimmedSelectedOverline] = color(0.30F, 0.46F, 0.54F, 1.0F);
    colors[ImGuiCol_PlotLines] = color(0.62F, 0.84F, 0.92F, 1.0F);
    colors[ImGuiCol_PlotLinesHovered] = color(0.78F, 0.90F, 0.96F, 1.0F);
    colors[ImGuiCol_PlotHistogram] = color(0.44F, 0.68F, 0.78F, 1.0F);
    colors[ImGuiCol_PlotHistogramHovered] = color(0.78F, 0.90F, 0.96F, 1.0F);
    colors[ImGuiCol_TableHeaderBg] = color(0.018F, 0.040F, 0.055F, 0.92F);
    colors[ImGuiCol_TableBorderStrong] = color(0.20F, 0.29F, 0.34F, 1.0F);
    colors[ImGuiCol_TableBorderLight] = color(0.12F, 0.18F, 0.22F, 1.0F);
    colors[ImGuiCol_TableRowBg] = color(0.0F, 0.0F, 0.0F, 0.0F);
    colors[ImGuiCol_TableRowBgAlt] = color(0.62F, 0.84F, 0.92F, 0.030F);
    colors[ImGuiCol_TextLink] = color(0.64F, 0.84F, 0.94F, 1.0F);
    colors[ImGuiCol_TextSelectedBg] = color(0.18F, 0.36F, 0.46F, 0.55F);
    colors[ImGuiCol_DragDropTarget] = color(0.74F, 0.90F, 0.96F, 0.90F);
    colors[ImGuiCol_NavCursor] = color(0.62F, 0.84F, 0.92F, 0.92F);
    colors[ImGuiCol_NavWindowingHighlight] = color(0.82F, 0.94F, 1.0F, 0.70F);
    colors[ImGuiCol_NavWindowingDimBg] = color(0.0F, 0.0F, 0.0F, 0.20F);
    colors[ImGuiCol_ModalWindowDimBg] = color(0.0F, 0.0F, 0.0F, 0.60F);
}

[[nodiscard]] cubey::vulkan::ImageLayoutTransition
present_to_color_attachment_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .src_access_mask = 0,
        .dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
}

[[nodiscard]] VkRenderingAttachmentInfo load_color_attachment(VkImageView view) {
    auto attachment =
        cubey::vulkan::vk_struct<VkRenderingAttachmentInfo>(
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    attachment.imageView = view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    return attachment;
}

} // namespace

ImGuiOverlay::~ImGuiOverlay() {
    destroy();
}

void ImGuiOverlay::create(const ImGuiOverlayCreateInfo& info) {
    validate_create_info(info);
    destroy();

    device_ = info.device->handle();
    command_pool_ = info.frame_resources->command_pool();
    color_format_ = info.swapchain->format();

    create_descriptor_pool();
    allocate_command_buffers(info.frame_resources->frame_slot_count());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    apply_cubey_debug_style();

    bool glfw_backend_started = false;
    try {
        if (!ImGui_ImplGlfw_InitForVulkan(info.window->native_handle(), true)) {
            throw std::runtime_error("Dear ImGui GLFW backend initialization failed");
        }
        glfw_backend_started = true;

        pipeline_rendering_info_ =
            cubey::vulkan::vk_struct<VkPipelineRenderingCreateInfoKHR>(
                VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR);
        pipeline_rendering_info_.colorAttachmentCount = 1;
        pipeline_rendering_info_.pColorAttachmentFormats = &color_format_;

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.ApiVersion = VK_API_VERSION_1_3;
        init_info.Instance = info.instance->handle();
        init_info.PhysicalDevice = info.device->physical_device();
        init_info.Device = info.device->handle();
        init_info.QueueFamily = info.device->queue_family();
        init_info.Queue = info.device->queue();
        init_info.DescriptorPool = descriptor_pool_;
        init_info.MinImageCount = static_cast<std::uint32_t>(info.swapchain->image_count());
        init_info.ImageCount = static_cast<std::uint32_t>(info.swapchain->image_count());
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_info_;
        init_info.UseDynamicRendering = true;
        init_info.CheckVkResultFn = check_imgui_vk_result;
        if (!ImGui_ImplVulkan_Init(&init_info)) {
            throw std::runtime_error("Dear ImGui Vulkan backend initialization failed");
        }
    } catch (...) {
        if (glfw_backend_started) {
            ImGui_ImplGlfw_Shutdown();
        }
        ImGui::DestroyContext();
        throw;
    }

    active_ = true;
}

void ImGuiOverlay::destroy() {
    if (frame_started_) {
        ImGui::EndFrame();
        frame_started_ = false;
    }

    if (active_) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        active_ = false;
    }

    if (!command_buffers_.empty() && device_ != VK_NULL_HANDLE &&
        command_pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_, command_pool_,
                             static_cast<std::uint32_t>(command_buffers_.size()),
                             command_buffers_.data());
    }
    command_buffers_.clear();

    if (descriptor_pool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    command_pool_ = VK_NULL_HANDLE;
    color_format_ = VK_FORMAT_UNDEFINED;
    pipeline_rendering_info_ = {};
    device_ = VK_NULL_HANDLE;
}

void ImGuiOverlay::begin_frame() {
    if (!active_) {
        return;
    }
    if (frame_started_) {
        ImGui::EndFrame();
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    frame_started_ = true;
}

void ImGuiOverlay::discard_frame() {
    if (!active_ || !frame_started_) {
        return;
    }
    ImGui::EndFrame();
    frame_started_ = false;
}

UiCaptureState ImGuiOverlay::capture_state() const {
    if (!active_ || !frame_started_) {
        return {};
    }
    const ImGuiIO& io = ImGui::GetIO();
    return {
        .wants_mouse = io.WantCaptureMouse,
        .wants_keyboard = io.WantCaptureKeyboard,
    };
}

VkCommandBuffer ImGuiOverlay::record(cubey::render::FrameSlot frame_slot,
                                     cubey::render::ColorTargetView color_target) {
    if (!active_ || !frame_started_) {
        return VK_NULL_HANDLE;
    }
    cubey::render::validate_frame_slot(frame_slot);
    if (static_cast<std::size_t>(frame_slot.count) != command_buffers_.size()) {
        throw std::runtime_error("ImGui overlay frame slot count does not match command buffers");
    }
    const std::size_t command_index = static_cast<std::size_t>(frame_slot.index);
    VkCommandBuffer command_buffer = command_buffers_.at(command_index);

    ImGui::Render();
    frame_started_ = false;

    cubey::vulkan::check(vkResetCommandBuffer(command_buffer, 0),
                         "vkResetCommandBuffer ImGui overlay");

    auto begin = cubey::vulkan::vk_struct<VkCommandBufferBeginInfo>(
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cubey::vulkan::check(vkBeginCommandBuffer(command_buffer, &begin),
                         "vkBeginCommandBuffer ImGui overlay");

    cubey::vulkan::transition_image_layout(
        command_buffer, present_to_color_attachment_transition(color_target.image));

    VkRenderingAttachmentInfo color_attachment = load_color_attachment(color_target.view);
    auto rendering =
        cubey::vulkan::vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea.extent = color_target.extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_attachment;
    vkCmdBeginRendering(command_buffer, &rendering);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
    vkCmdEndRendering(command_buffer);

    cubey::vulkan::transition_image_layout(
        command_buffer,
        cubey::vulkan::finish_color_attachment_for_present_transition(color_target.image));

    cubey::vulkan::check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer ImGui overlay");
    return command_buffer;
}

void ImGuiOverlay::create_descriptor_pool() {
    constexpr std::uint32_t kDescriptorsPerType = 64;
    const std::array<VkDescriptorPoolSize, 11> pool_sizes{{
        {VK_DESCRIPTOR_TYPE_SAMPLER, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kDescriptorsPerType},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, kDescriptorsPerType},
    }};

    auto pool_info =
        cubey::vulkan::vk_struct<VkDescriptorPoolCreateInfo>(
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = kDescriptorsPerType * static_cast<std::uint32_t>(pool_sizes.size());
    pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    cubey::vulkan::check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_),
                         "vkCreateDescriptorPool ImGui overlay");
}

void ImGuiOverlay::allocate_command_buffers(std::uint32_t frame_slot_count) {
    if (frame_slot_count == 0) {
        throw std::runtime_error("ImGui overlay requires at least one frame slot");
    }
    command_buffers_.assign(frame_slot_count, VK_NULL_HANDLE);
    auto alloc =
        cubey::vulkan::vk_struct<VkCommandBufferAllocateInfo>(
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = frame_slot_count;
    cubey::vulkan::check(vkAllocateCommandBuffers(device_, &alloc, command_buffers_.data()),
                         "vkAllocateCommandBuffers ImGui overlay");
}

} // namespace cubey::host
