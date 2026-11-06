#pragma once
#include "raytracing_layout.h"
#include "raytracing_materials.h"
#include "raytracing_scene.h"
#include <cubey/core/profiling.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/vk_check.h>
#include <memory>
#include <nlohmann/json.hpp>

namespace cubey::projects::raytracing {
class Environment;
struct RtFunctions {
    PFN_vkCreateAccelerationStructureKHR create_as{};
    PFN_vkDestroyAccelerationStructureKHR destroy_as{};
    PFN_vkGetAccelerationStructureBuildSizesKHR build_sizes{};
    PFN_vkCmdBuildAccelerationStructuresKHR build{};
    PFN_vkGetAccelerationStructureDeviceAddressKHR as_address{};
    PFN_vkCreateRayTracingPipelinesKHR create_pipeline{};
    PFN_vkGetRayTracingShaderGroupHandlesKHR handles{};
    PFN_vkCmdTraceRaysKHR trace{};
};
struct CameraUniform {
    math::Mat4 inverse_view;
    math::Vec4 eye, sun, light, base, options;
};
struct GpuInstance {
    math::Mat4 world, normal_world;
    glm::uvec4 offsets;
    math::Vec4 color;
};
static_assert(sizeof(Vertex) == 32 && sizeof(GpuInstance) == 160 && sizeof(CameraUniform) == 144);
CameraUniform camera_uniform(const Config& c, VkExtent2D extent);
class Renderer {
  public:
    Renderer(vulkan::Device& device, vulkan::GpuRuntime& gpu, const Scene& scene,
             std::uint32_t slots, std::string_view fault = "none", bool diagnostic = false,
             bool collect_metrics = false, const Config* config = nullptr);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(VkExtent2D extent, VkFormat output_format, bool fault = false);
    void render(VkCommandBuffer command, render::ColorTargetView output, render::FrameSlot slot,
                const Config& config, bool present, profiling::ProfileRecorder* profile,
                std::uint64_t frame);
    void finish();
    void save_queries(const std::filesystem::path& path) const;
    [[nodiscard]] nlohmann::json metrics() const;
    [[nodiscard]] std::uint32_t sample_count() const {
        return history_samples_;
    }
    [[nodiscard]] std::uint64_t history_epoch() const {
        return history_epochs_;
    }

  private:
    void destroy() noexcept;
    void create_scene(vulkan::GpuRuntime& gpu, const Scene& scene);
    void create_pipeline();
    void collect(std::uint32_t slot);
    std::uint64_t requested_bytes() const;
    struct Acceleration {
        VkAccelerationStructureKHR handle{};
        std::unique_ptr<vulkan::Buffer> storage;
        VkDeviceAddress address{};
    };
    struct Target {
        std::unique_ptr<render::Texture2D> color;
        std::unique_ptr<vulkan::Buffer> queries;
        bool initialized = false;
    };
    vulkan::Device& device_;
    RtFunctions rt_;
    std::uint32_t slots_;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR properties_ =
        vulkan::vk_struct<VkPhysicalDeviceRayTracingPipelinePropertiesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
    VkPhysicalDeviceAccelerationStructurePropertiesKHR as_properties_ =
        vulkan::vk_struct<VkPhysicalDeviceAccelerationStructurePropertiesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
    std::vector<Acceleration> accelerations_;
    std::unique_ptr<vulkan::Buffer> vertices_, indices_, instances_, sbt_;
    std::vector<std::unique_ptr<vulkan::Buffer>> uniforms_;
    std::vector<std::unique_ptr<vulkan::Buffer>> environment_uniforms_, material_buffers_;
    std::vector<OpticalMaterial> material_values_;
    std::shared_ptr<WorkshopState> workshop_;
    std::vector<std::unique_ptr<vulkan::Buffer>> recipe_buffers_, workshop_uniforms_;
    std::unique_ptr<vulkan::Image> history_;
    std::vector<std::byte> history_identity_;
    std::uint32_t history_samples_ = 0;
    std::uint64_t history_epochs_ = 0;
    bool history_initialized_ = false;
    std::unique_ptr<Environment> environment_;
    std::vector<math::Vec4> glass_planes_;
    std::unique_ptr<vulkan::DescriptorSetArray> descriptors_;
    VkPipelineLayout layout_{};
    VkPipeline pipeline_{};
    VkStridedDeviceAddressRegionKHR raygen_{}, miss_{}, hit_{}, callable_{};
    std::vector<Target> targets_;
    VkExtent2D extent_{};
    VkFormat output_format_{};
    render::HdrPostFrame post_;
    render::RenderGraphFrameExecutor executor_;
    std::optional<vulkan::GpuTimestampProfiler> trace_timer_, whole_timer_;
    std::optional<vulkan::GpuTimestampProfiler> environment_timer_, total_timer_;
    std::vector<std::optional<std::uint64_t>> recorded_;
    profiling::ProfileRecorder* profile_ = nullptr;
    nlohmann::json timing_ = nlohmann::json::array();
    nlohmann::json sampling_frames_ = nlohmann::json::array();
    double scene_ms_ = 0, pipeline_ms_ = 0, as_build_ms_ = 0, as_gpu_ms_ = 0;
    std::uint64_t geometry_bytes_ = 0, as_bytes_ = 0, scratch_peak_ = 0, triangle_count_ = 0;
    std::uint64_t target_generations_ = 0;
    std::uint64_t requested_peak_ = 0;
    bool diagnostic_ = false, collect_metrics_ = false;
    std::uint64_t dropped_samples_ = 0;
    std::optional<std::uint32_t> last_slot_;
    int dump_mode_ = 0;
    std::uint64_t dump_stride_ = 64;
};
} // namespace cubey::projects::raytracing
