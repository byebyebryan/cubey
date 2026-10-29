// Separate, compile-only Vulkan diagnostic. Never enables features on the app's device.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>
using Json = nlohmann::json;
namespace fs = std::filesystem;
void check(VkResult r) {
    if (r != VK_SUCCESS)
        throw std::runtime_error("Vulkan result " + std::to_string(r));
}
std::vector<std::uint32_t> read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() % 4 != 0)
        throw std::runtime_error("invalid SPIR-V: " + path.string());
    std::vector<std::uint32_t> bytes(static_cast<std::size_t>(f.tellg()) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size() * 4));
    return bytes;
}
int main(int argc, char** argv) {
    try {
        if (argc < 4)
            throw std::invalid_argument(
                "shader-directory output-directory fragment.spv[@single] ...");
        const fs::path shaders = argv[1], output = argv[2];
        fs::create_directories(output);
        const VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                    nullptr,
                                    "Cubey shader compiler diagnostic",
                                    1,
                                    nullptr,
                                    0,
                                    VK_API_VERSION_1_3};
        const char* validation = "VK_LAYER_KHRONOS_validation";
        const VkInstanceCreateInfo ic{
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 1, &validation, 0, nullptr};
        VkInstance instance;
        check(vkCreateInstance(&ic, nullptr, &instance));
        std::uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        std::vector<VkPhysicalDevice> physicals(count);
        check(vkEnumeratePhysicalDevices(instance, &count, physicals.data()));
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        for (const auto candidate : physicals) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(candidate, &p);
            if (p.vendorID == 0x10de) {
                physical = candidate;
                break;
            }
        }
        if (!physical)
            throw std::runtime_error("no NVIDIA device; diagnostic scope is the measured device");
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical, &properties);
        VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR executable{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
        VkPhysicalDeviceVulkan13Features core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                              &executable};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &core};
        vkGetPhysicalDeviceFeatures2(physical, &features);
        if (!executable.pipelineExecutableInfo || !core.dynamicRendering)
            throw std::runtime_error("required diagnostic features unavailable");
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
        std::uint32_t family = 0;
        while (family < count && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            ++family;
        if (family == count)
            throw std::runtime_error("no graphics queue");
        const float priority = 1;
        const VkDeviceQueueCreateInfo qi{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, family, 1, &priority};
        const char* extension = VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME;
        // Enable only the two diagnostic requirements, not all supported features.
        core = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &executable};
        core.dynamicRendering = VK_TRUE;
        executable.pipelineExecutableInfo = VK_TRUE;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              &core,
                              0,
                              1,
                              &qi,
                              0,
                              nullptr,
                              1,
                              &extension,
                              nullptr};
        VkDevice device;
        check(vkCreateDevice(physical, &dc, nullptr, &device));
        const auto getProperties = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
            vkGetDeviceProcAddr(device, "vkGetPipelineExecutablePropertiesKHR"));
        const auto getStatistics = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
            vkGetDeviceProcAddr(device, "vkGetPipelineExecutableStatisticsKHR"));
        const auto getIR = reinterpret_cast<PFN_vkGetPipelineExecutableInternalRepresentationsKHR>(
            vkGetDeviceProcAddr(device, "vkGetPipelineExecutableInternalRepresentationsKHR"));
        if (!getProperties || !getStatistics || !getIR)
            throw std::runtime_error("missing executable diagnostic entry point");
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        // Prepared source pipelines read binding 19. Keep the diagnostic layout
        // compatible with the actual renderer before querying compiler statistics.
        for (unsigned i = 0; i <= 19; ++i) {
            const auto type = i == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                              : i == 1 || (i >= 8 && i <= 12)
                                  ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                  : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings.push_back({i, type, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        }
        const VkDescriptorSetLayoutCreateInfo dl{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            static_cast<unsigned>(bindings.size()), bindings.data()};
        VkDescriptorSetLayout descriptor;
        check(vkCreateDescriptorSetLayout(device, &dl, nullptr, &descriptor));
        const VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
        const VkPipelineLayoutCreateInfo pl{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &descriptor, 1, &push};
        VkPipelineLayout layout;
        check(vkCreatePipelineLayout(device, &pl, nullptr, &layout));
        const auto module = [&](const fs::path& path) {
            const auto bytes = read(path);
            const VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr,
                                              0, bytes.size() * 4, bytes.data()};
            VkShaderModule m;
            check(vkCreateShaderModule(device, &mi, nullptr, &m));
            return m;
        };
        const auto vertex = module(shaders / "sdf.vert.spv");
        const VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        const VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
        const VkViewport viewport{0, 0, 1920, 1080, 0, 1};
        const VkRect2D scissor{{0, 0}, {1920, 1080}};
        const VkPipelineViewportStateCreateInfo vp{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            nullptr,
            0,
            1,
            &viewport,
            1,
            &scissor};
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        const VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0,
            VK_SAMPLE_COUNT_1_BIT};
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = 15;
        const VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_FALSE,
            VK_LOGIC_OP_COPY,
            1,
            &attachment,
            {}};
        const VkFormat color = VK_FORMAT_R16G16B16A16_SFLOAT;
        const VkPipelineRenderingCreateInfo rendering{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            nullptr,
            0,
            1,
            &color,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_UNDEFINED};
        Json result{{"device", properties.deviceName},
                    {"driver_version_raw", properties.driverVersion},
                    {"scope", "Separate compile-only Vulkan pipeline. Resource statistics do not "
                              "measure live occupancy or execution time."},
                    {"pipelines", Json::array()}};
        result["local_memory_statistic_usable"] = true;
        for (int arg = 3; arg < argc; ++arg) {
            const std::string request = argv[arg];
            const bool single_material = request.ends_with("@single");
            const fs::path name = single_material ? request.substr(0, request.size() - 7) : request;
            if (name.has_parent_path())
                throw std::invalid_argument("fragment name must be a basename");
            const auto fragment = module(shaders / name);
            const VkBool32 single = VK_TRUE;
            const VkSpecializationMapEntry entry{0, 0, sizeof(single)};
            const VkSpecializationInfo specialization{1, &entry, sizeof(single), &single};
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
                {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                  VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                  VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main",
                  single_material ? &specialization : nullptr}}};
            VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                            &rendering};
            ci.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR |
                       VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
            ci.stageCount = 2;
            ci.pStages = stages.data();
            ci.pVertexInputState = &vi;
            ci.pInputAssemblyState = &ia;
            ci.pViewportState = &vp;
            ci.pRasterizationState = &raster;
            ci.pMultisampleState = &ms;
            ci.pDepthStencilState = &depth;
            ci.pColorBlendState = &blend;
            ci.layout = layout;
            VkPipeline pipeline;
            const auto start = std::chrono::steady_clock::now();
            check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline));
            const auto setup_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            VkPipelineInfoKHR pi{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR, nullptr, pipeline};
            count = 0;
            check(getProperties(device, &pi, &count, nullptr));
            std::vector<VkPipelineExecutablePropertiesKHR> executables(
                count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
            check(getProperties(device, &pi, &count, executables.data()));
            Json row{{"request", request},
                     {"shader", name.string()},
                     {"single_material", single_material},
                     {"pipeline_setup_ms", setup_ms},
                     {"executables", Json::array()}};
            for (unsigned e = 0; e < count; ++e) {
                const auto& prop = executables[e];
                Json erow{{"name", prop.name},           {"description", prop.description},
                          {"stages", prop.stages},       {"subgroup_size", prop.subgroupSize},
                          {"statistics", Json::array()}, {"representations", Json::array()}};
                const VkPipelineExecutableInfoKHR ei{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
                                                     nullptr, pipeline, e};
                unsigned n = 0;
                check(getStatistics(device, &ei, &n, nullptr));
                std::vector<VkPipelineExecutableStatisticKHR> statistics(n);
                // Explicitly clear every union byte, including inactive-member
                // padding, before asking the driver to fill its statistics.
                std::memset(statistics.data(), 0, statistics.size() * sizeof(statistics[0]));
                for (auto& stat : statistics)
                    stat.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
                check(getStatistics(device, &ei, &n, statistics.data()));
                for (const auto& stat : statistics) {
                    Json value;
                    if (stat.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR)
                        value = bool(stat.value.b32);
                    else if (stat.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR)
                        value = stat.value.i64;
                    else if (stat.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR)
                        value = stat.value.u64;
                    else
                        value = stat.value.f64;
                    erow["statistics"].push_back({{"name", stat.name},
                                                  {"description", stat.description},
                                                  {"format", stat.format},
                                                  {"raw_union_u64", stat.value.u64},
                                                  {"value", value}});
                    if (prop.stages == VK_SHADER_STAGE_VERTEX_BIT &&
                        std::string(stat.name) == "Local Memory Size" &&
                        stat.value.u64 >= (1ULL << 32)) {
                        result["local_memory_statistic_usable"] = false;
                        result["local_memory_limitation"] =
                            "Driver reports at least 4 GiB per-thread local memory for the trivial "
                            "fullscreen vertex shader. Raw values retained but excluded from "
                            "conclusions; no low-bit reinterpretation.";
                    }
                }
                n = 0;
                check(getIR(device, &ei, &n, nullptr));
                std::vector<VkPipelineExecutableInternalRepresentationKHR> ir(
                    n, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR});
                check(getIR(device, &ei, &n, ir.data()));
                std::vector<std::vector<char>> storage(n);
                for (unsigned i = 0; i < n; ++i) {
                    storage[i].resize(ir[i].dataSize);
                    ir[i].pData = storage[i].data();
                }
                check(getIR(device, &ei, &n, ir.data()));
                for (unsigned i = 0; i < n; ++i) {
                    const auto file = name.string() + ".exe" + std::to_string(e) + ".ir" +
                                      std::to_string(i) + (ir[i].isText ? ".txt" : ".bin");
                    std::ofstream f(output / file, std::ios::binary);
                    f.write(storage[i].data(), static_cast<std::streamsize>(ir[i].dataSize));
                    erow["representations"].push_back({{"name", ir[i].name},
                                                       {"description", ir[i].description},
                                                       {"text", bool(ir[i].isText)},
                                                       {"bytes", ir[i].dataSize},
                                                       {"file", file}});
                }
                row["executables"].push_back(erow);
            }
            result["pipelines"].push_back(row);
            std::ofstream(output / "statistics.json") << result.dump(2) << '\n';
            std::cout << row.dump() << '\n';
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyShaderModule(device, fragment, nullptr);
        }
        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptor, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
