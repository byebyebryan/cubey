#include <cubey/host/dlss_upscaler.h>

#include <stdexcept>

#ifdef CUBEY_DLSS_FEATURE_DIR
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/vk_check.h>

#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <utility>
#endif

namespace cubey::host {
#ifdef CUBEY_DLSS_FEATURE_DIR
namespace {

constexpr char project_id[] = "702455f3-f733-4dd9-86d6-eb296e458760";
constexpr char engine_version[] = "0.1.0";
constexpr auto quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;

NVSDK_NGX_DLSS_Hint_Render_Preset ngx_quality_preset(std::string_view preset) {
    if (preset == "k")
        return NVSDK_NGX_DLSS_Hint_Render_Preset_K;
    if (preset == "m")
        return NVSDK_NGX_DLSS_Hint_Render_Preset_M;
    if (preset == "l")
        return NVSDK_NGX_DLSS_Hint_Render_Preset_L;
    throw std::invalid_argument("unknown DLSS Quality preset");
}

void check_ngx(NVSDK_NGX_Result result, const char* operation) {
    if (NVSDK_NGX_FAILED(result)) {
        const std::wstring_view description = GetNGXResultAsString(result);
        std::ostringstream message;
        message << "DLSS " << operation
                << " failed: " << std::string(description.begin(), description.end()) << " (0x"
                << std::hex << static_cast<unsigned>(result) << ')';
        throw std::runtime_error(message.str());
    }
}

std::filesystem::path app_data_directory() {
    std::filesystem::path root;
    if (const char* cache = std::getenv("XDG_CACHE_HOME");
        cache && std::filesystem::path(cache).is_absolute()) {
        root = cache;
    } else if (const char* home = std::getenv("HOME");
               home && std::filesystem::path(home).is_absolute()) {
        root = std::filesystem::path(home) / ".cache";
    } else {
        throw std::runtime_error("DLSS requires a writable user cache (XDG_CACHE_HOME or HOME)");
    }
    const auto directory = root / "cubey" / "ngx";
    std::filesystem::create_directories(directory);
    return directory;
}

// NGX borrows wide-string pointers from both structures.
struct NgxConfiguration {
    std::wstring app_data = app_data_directory().wstring();
    std::wstring feature_dir = std::filesystem::path(CUBEY_DLSS_FEATURE_DIR).wstring();
    const wchar_t* search_path = feature_dir.c_str();
    NVSDK_NGX_FeatureCommonInfo common{};

    NgxConfiguration() {
        const auto runtime =
            std::filesystem::path(CUBEY_DLSS_FEATURE_DIR) / "libnvidia-ngx-dlss.so.310.9.16";
        if (!std::filesystem::is_regular_file(runtime)) {
            throw std::runtime_error("DLSS SDK feature library missing: " + runtime.string());
        }
        common.PathListInfo = {&search_path, 1};
    }
    NgxConfiguration(const NgxConfiguration&) = delete;
    NgxConfiguration& operator=(const NgxConfiguration&) = delete;

    [[nodiscard]] NVSDK_NGX_FeatureDiscoveryInfo discovery() const {
        NVSDK_NGX_FeatureDiscoveryInfo info{};
        info.SDKVersion = NVSDK_NGX_Version_API;
        info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
        info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
        info.Identifier.v.ProjectDesc = {project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, engine_version};
        info.ApplicationDataPath = app_data.c_str();
        info.FeatureInfo = &common;
        return info;
    }
};

std::vector<std::string> extension_names(const VkExtensionProperties* properties,
                                         std::uint32_t count) {
    std::vector<std::string> names;
    names.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        names.emplace_back(properties[index].extensionName);
    }
    return names;
}

bool same_extent(VkExtent2D a, VkExtent2D b) {
    return a.width == b.width && a.height == b.height;
}

NVSDK_NGX_Resource_VK image_resource(const render::RenderGraphExecutionContext& context,
                                     render::RenderGraphTextureHandle handle, VkExtent2D expected,
                                     bool writable = false) {
    const auto& desc = context.texture(handle).desc;
    if (desc.extent.width != expected.width || desc.extent.height != expected.height ||
        desc.extent.depth != 1) {
        throw std::logic_error("DLSS graph texture size mismatch: " + desc.label);
    }
    const auto image = context.resolved_texture(handle);
    const VkImageSubresourceRange range{desc.aspects, 0, 1, 0, 1};
    return NVSDK_NGX_Create_ImageView_Resource_VK(image.view, image.image, range, desc.format,
                                                  expected.width, expected.height, writable);
}

struct FeatureDeleter {
    void operator()(NVSDK_NGX_Handle* handle) const noexcept {
        NVSDK_NGX_VULKAN_ReleaseFeature(handle);
    }
};
using Feature = std::unique_ptr<NVSDK_NGX_Handle, FeatureDeleter>;

} // namespace

struct DlssUpscaler::Impl {
    NgxConfiguration config;
    VkDevice device = VK_NULL_HANDLE;
    NVSDK_NGX_Parameter* parameters = nullptr;
    Feature feature;
    VkExtent2D render_extent{};
    VkExtent2D output_extent{};

    ~Impl() {
        feature.reset();
        if (parameters)
            NVSDK_NGX_VULKAN_DestroyParameters(parameters);
        if (device)
            NVSDK_NGX_VULKAN_Shutdown1(device);
    }
};

VulkanRequirements DlssUpscaler::vulkan_requirements() {
    auto config = std::make_shared<NgxConfiguration>();
    const auto discovery = config->discovery();
    std::uint32_t count = 0;
    VkExtensionProperties* properties = nullptr;
    check_ngx(
        NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&discovery, &count, &properties),
        "instance extension discovery");
    VulkanRequirements requirements;
    requirements.instance_extensions = extension_names(properties, count);
    requirements.device.buffer_device_address = true;
    requirements.device.query_extensions =
        [config = std::move(config)](
            VkInstance instance,
            VkPhysicalDevice adapter) -> std::optional<std::vector<std::string>> {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(adapter, &properties);
        if (properties.vendorID != 0x10de)
            return std::nullopt;
        const auto discovery = config->discovery();
        NVSDK_NGX_FeatureRequirement support{};
        const auto result =
            NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, adapter, &discovery, &support);
        if (NVSDK_NGX_FAILED(result) ||
            support.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported) {
            return std::nullopt;
        }
        std::uint32_t count = 0;
        VkExtensionProperties* extensions = nullptr;
        check_ngx(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
                      instance, adapter, &discovery, &count, &extensions),
                  "device extension discovery");
        return extension_names(extensions, count);
    };
    return requirements;
}

DlssUpscaler::DlssUpscaler(VkInstance instance, const vulkan::Device& device,
                           std::string_view preset)
    : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    check_ngx(NVSDK_NGX_VULKAN_Init_with_ProjectID(
                  project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, engine_version,
                  p.config.app_data.c_str(), instance, device.physical_device(), device.handle(),
                  vkGetInstanceProcAddr, vkGetDeviceProcAddr, &p.config.common),
              "initialization");
    p.device = device.handle();
    check_ngx(NVSDK_NGX_VULKAN_GetCapabilityParameters(&p.parameters), "capability query");
    int available = 0;
    check_ngx(p.parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available),
              "availability query");
    if (available == 0)
        throw std::runtime_error("DLSS Super Resolution unavailable on the selected device");
    NVSDK_NGX_Parameter_SetUI(p.parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                              static_cast<unsigned int>(ngx_quality_preset(preset)));
}

VkExtent2D DlssUpscaler::create_target(vulkan::GpuOwnerContext& context,
                                         VkExtent2D output_extent) {
    context.require_owner_thread("DLSS target creation");
    auto& p = *impl_;
    if (context.device().handle() != p.device)
        throw std::logic_error("DLSS target device mismatch");
    VkExtent2D optimal{}, maximum{}, minimum{};
    float sharpness = 0;
    check_ngx(NGX_DLSS_GET_OPTIMAL_SETTINGS(p.parameters, output_extent.width, output_extent.height,
                                            quality, &optimal.width, &optimal.height,
                                            &maximum.width, &maximum.height, &minimum.width,
                                            &minimum.height, &sharpness),
              "Quality extent query");
    if (optimal.width == 0 || optimal.height == 0 || optimal.width > output_extent.width ||
        optimal.height > output_extent.height) {
        throw std::runtime_error("DLSS returned an invalid Quality render extent");
    }

    destroy_target();
    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature.InWidth = optimal.width;
    create.Feature.InHeight = optimal.height;
    create.Feature.InTargetWidth = output_extent.width;
    create.Feature.InTargetHeight = output_extent.height;
    create.Feature.InPerfQualityValue = quality;
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                                  NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                  NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    vulkan::ImmediateCommands commands(context);
    NVSDK_NGX_Handle* handle = nullptr;
    const auto result = NGX_VULKAN_CREATE_DLSS_EXT1(p.device, commands.command_buffer(), 1, 1,
                                                    &handle, p.parameters, &create);
    Feature candidate(handle);
    check_ngx(result, "Quality feature creation");
    commands.submit_and_wait();
    p.feature = std::move(candidate);
    p.render_extent = optimal;
    p.output_extent = output_extent;
    return optimal;
}

void DlssUpscaler::destroy_target() noexcept {
    impl_->feature.reset();
    impl_->render_extent = {};
    impl_->output_extent = {};
}

render::RenderGraphTextureHandle
DlssUpscaler::declare(render::RenderGraphBuilder& graph,
                        const render::TemporalUpscaleInputs& inputs) {
    auto& p = *impl_;
    if (!p.feature || !same_extent(p.render_extent, inputs.view.extent) ||
        !same_extent(p.output_extent, inputs.output_extent)) {
        throw std::logic_error("DLSS frame does not match its target");
    }
    const auto output = graph.create_texture(
        {.label = "dlss.resolved",
         .extent = {inputs.output_extent.width, inputs.output_extent.height, 1},
         .format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .aspects = VK_IMAGE_ASPECT_COLOR_BIT});
    graph.add_pass("dlss.evaluate", render::RenderGraphQueueDomain::Compute)
        .read_texture(inputs.color)
        .read_texture(inputs.depth)
        .read_texture(inputs.motion)
        .write_storage_texture_with_transfer_clear(output)
        .execute([&p, output, color_handle = inputs.color, depth_handle = inputs.depth,
                  motion_handle = inputs.motion, jitter = inputs.view.sample_offset_pixels,
                  reset = !inputs.view.history_valid, frame_ms = inputs.frame_time_ms](
                     const render::RenderGraphExecutionContext& context) {
            auto color = image_resource(context, color_handle, p.render_extent);
            auto depth = image_resource(context, depth_handle, p.render_extent);
            auto motion = image_resource(context, motion_handle, p.render_extent);
            auto resolved = image_resource(context, output, p.output_extent, true);

            NVSDK_NGX_VK_DLSS_Eval_Params eval{};
            eval.Feature.pInColor = &color;
            eval.Feature.pInOutput = &resolved;
            eval.pInDepth = &depth;
            eval.pInMotionVectors = &motion;
            eval.InRenderSubrectDimensions = {p.render_extent.width, p.render_extent.height};
            // Cubey stores the sample position within the pixel. NGX expects the projected image
            // displacement.
            eval.InJitterOffsetX = -jitter.x;
            eval.InJitterOffsetY = -jitter.y;
            eval.InMVScaleX = eval.InMVScaleY = 1.0F;
            eval.InPreExposure = eval.InExposureScale = 1.0F;
            eval.InReset = reset;
            eval.InFrameTimeDeltaInMsec = frame_ms;
            const auto cmd = context.recorder().handle();
            // NGX's persistent history is private to the SDK and absent from the graph's per-image
            // barriers.
            VkMemoryBarrier history =
                vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
            history.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            history.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                    VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            constexpr VkPipelineStageFlags stages =
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            vkCmdPipelineBarrier(cmd, stages, stages, 0, 1, &history, 0, nullptr, 0, nullptr);
            check_ngx(NGX_VULKAN_EVALUATE_DLSS_EXT(cmd, p.feature.get(), p.parameters, &eval),
                      "evaluation");
        });
    return output;
}

#else
namespace {
[[noreturn]] void sdk_required() {
    throw std::runtime_error("DLSS support is disabled in this build. Configure CUBEY_DLSS_SDK_DIR "
                             "with the NVIDIA DLSS v310.9.16 SDK.");
}
} // namespace

struct DlssUpscaler::Impl {};
VulkanRequirements DlssUpscaler::vulkan_requirements() {
    sdk_required();
}
DlssUpscaler::DlssUpscaler(VkInstance, const vulkan::Device&, std::string_view) {
    sdk_required();
}
VkExtent2D DlssUpscaler::create_target(vulkan::GpuOwnerContext&, VkExtent2D) {
    sdk_required();
}
void DlssUpscaler::destroy_target() noexcept {}
render::RenderGraphTextureHandle DlssUpscaler::declare(render::RenderGraphBuilder&,
                                                         const render::TemporalUpscaleInputs&) {
    sdk_required();
}
#endif

DlssUpscaler::~DlssUpscaler() = default;

} // namespace cubey::host
