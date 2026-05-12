#pragma once

#include <cubey/render/frame_data.h>
#include <cubey/render/material.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cubey::render {

struct RenderGraphTextureHandle {
    std::uint32_t index = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(RenderGraphTextureHandle lhs, RenderGraphTextureHandle rhs) = default;
};

struct RenderGraphBufferHandle {
    std::uint32_t index = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(RenderGraphBufferHandle lhs, RenderGraphBufferHandle rhs) = default;
};

enum class RenderGraphResourceLifetime : std::uint8_t {
    Imported,
    Transient,
};

enum class RenderGraphQueueDomain : std::uint8_t {
    Graphics,
    Compute,
    Transfer,
};

enum class RenderGraphTextureUsage : std::uint8_t {
    SampledRead,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    ColorAttachment,
    DepthAttachment,
    TransferRead,
    TransferWrite,
};

enum class RenderGraphBufferUsage : std::uint8_t {
    UniformRead,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    TransferRead,
    TransferWrite,
};

enum class RenderGraphBarrierPhase : std::uint8_t {
    BeforePass,
    AfterPass,
};

struct RenderGraphTextureState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags access_mask = 0;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    friend bool operator==(RenderGraphTextureState lhs, RenderGraphTextureState rhs) = default;
};

struct RenderGraphBufferState {
    VkAccessFlags access_mask = 0;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    friend bool operator==(RenderGraphBufferState lhs, RenderGraphBufferState rhs) = default;
};

struct RenderGraphTextureDesc {
    std::string label{};
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspects = 0;
};

struct RenderGraphBufferDesc {
    std::string label{};
    VkDeviceSize byte_size = 0;
};

struct RenderGraphTextureResource {
    RenderGraphTextureHandle handle{};
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
    RenderGraphTextureDesc desc{};
    VkImage imported_image = VK_NULL_HANDLE;
    VkImageView imported_view = VK_NULL_HANDLE;
    std::optional<RenderGraphTextureState> initial_state{};
    std::optional<RenderGraphTextureState> final_state{};
};

struct RenderGraphBufferResource {
    RenderGraphBufferHandle handle{};
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
    RenderGraphBufferDesc desc{};
    VkBuffer imported_buffer = VK_NULL_HANDLE;
    std::optional<RenderGraphBufferState> initial_state{};
    std::optional<RenderGraphBufferState> final_state{};
};

struct RenderGraphTextureAccess {
    RenderGraphTextureHandle handle{};
    RenderGraphTextureUsage usage = RenderGraphTextureUsage::SampledRead;
};

struct RenderGraphBufferAccess {
    RenderGraphBufferHandle handle{};
    RenderGraphBufferUsage usage = RenderGraphBufferUsage::UniformRead;
};

struct RenderGraphTextureBarrier {
    RenderGraphTextureHandle handle{};
    std::optional<std::size_t> source_pass_index{};
    std::optional<RenderGraphTextureUsage> source_usage{};
    std::optional<RenderGraphTextureUsage> destination_usage{};
    RenderGraphTextureState source_state{};
    RenderGraphTextureState destination_state{};
};

struct RenderGraphBufferBarrier {
    RenderGraphBufferHandle handle{};
    std::optional<std::size_t> source_pass_index{};
    std::optional<RenderGraphBufferUsage> source_usage{};
    std::optional<RenderGraphBufferUsage> destination_usage{};
    RenderGraphBufferState source_state{};
    RenderGraphBufferState destination_state{};
};

class CompiledRenderGraph;
class RenderGraphResourceSet;
struct RenderGraphCompiledPass;

struct RenderGraphResolvedTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct RenderGraphSampledTextureView {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct RenderGraphResolvedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize byte_size = 0;
};

class RenderGraphExecutionContext {
  public:
    [[nodiscard]] const CompiledRenderGraph& graph() const;
    [[nodiscard]] const RenderGraphCompiledPass& pass() const;
    [[nodiscard]] const cubey::vulkan::CommandRecorder& recorder() const;
    [[nodiscard]] std::size_t pass_index() const noexcept {
        return pass_index_;
    }
    [[nodiscard]] const RenderGraphTextureResource& texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] const RenderGraphBufferResource& buffer(RenderGraphBufferHandle handle) const;
    [[nodiscard]] RenderGraphResolvedTexture
    resolved_texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] RenderGraphResolvedBuffer resolved_buffer(RenderGraphBufferHandle handle) const;

  private:
    friend class CompiledRenderGraph;

    RenderGraphExecutionContext(const CompiledRenderGraph& graph, std::size_t pass_index,
                                const RenderGraphResourceSet* resources,
                                const cubey::vulkan::CommandRecorder* recorder);

    const CompiledRenderGraph* graph_ = nullptr;
    const RenderGraphResourceSet* resources_ = nullptr;
    const cubey::vulkan::CommandRecorder* recorder_ = nullptr;
    std::size_t pass_index_ = 0;
};

using RenderGraphExecuteCallback = std::function<void(const RenderGraphExecutionContext&)>;

struct RenderGraphCompiledPass {
    std::string label{};
    RenderGraphQueueDomain queue_domain = RenderGraphQueueDomain::Graphics;
    std::vector<RenderGraphTextureAccess> texture_accesses{};
    std::vector<RenderGraphBufferAccess> buffer_accesses{};
    std::vector<RenderGraphTextureBarrier> before_texture_barriers{};
    std::vector<RenderGraphBufferBarrier> before_buffer_barriers{};
    std::vector<RenderGraphTextureBarrier> after_texture_barriers{};
    std::vector<RenderGraphBufferBarrier> after_buffer_barriers{};
    std::optional<MaterialPassInfo> material_pass{};
    RenderGraphExecuteCallback execute{};
};

class CompiledRenderGraph {
  public:
    CompiledRenderGraph() = default;
    CompiledRenderGraph(std::vector<RenderGraphTextureResource> textures,
                        std::vector<RenderGraphBufferResource> buffers,
                        std::vector<RenderGraphCompiledPass> passes);

    [[nodiscard]] const std::vector<RenderGraphTextureResource>& textures() const noexcept {
        return textures_;
    }

    [[nodiscard]] const std::vector<RenderGraphBufferResource>& buffers() const noexcept {
        return buffers_;
    }

    [[nodiscard]] const std::vector<RenderGraphCompiledPass>& passes() const noexcept {
        return passes_;
    }

    [[nodiscard]] const RenderGraphTextureResource& texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] const RenderGraphBufferResource& buffer(RenderGraphBufferHandle handle) const;
    void execute() const;
    void execute(const RenderGraphResourceSet& resources) const;
    void execute(const RenderGraphResourceSet& resources,
                 const cubey::vulkan::CommandRecorder& recorder) const;

  private:
    void execute(const RenderGraphResourceSet* resources,
                 const cubey::vulkan::CommandRecorder* recorder) const;

    std::vector<RenderGraphTextureResource> textures_{};
    std::vector<RenderGraphBufferResource> buffers_{};
    std::vector<RenderGraphCompiledPass> passes_{};
};

class RenderGraphResourceSet {
  public:
    explicit RenderGraphResourceSet(const CompiledRenderGraph& graph);
    RenderGraphResourceSet(const cubey::vulkan::Device& device, const CompiledRenderGraph& graph);
    ~RenderGraphResourceSet() = default;

    RenderGraphResourceSet(const RenderGraphResourceSet&) = delete;
    RenderGraphResourceSet& operator=(const RenderGraphResourceSet&) = delete;
    RenderGraphResourceSet(RenderGraphResourceSet&& other) noexcept = default;
    RenderGraphResourceSet& operator=(RenderGraphResourceSet&& other) noexcept = default;

    void bind_texture(RenderGraphTextureHandle handle, RenderGraphResolvedTexture texture);
    void bind_buffer(RenderGraphBufferHandle handle, RenderGraphResolvedBuffer buffer);

    [[nodiscard]] std::optional<RenderGraphResolvedTexture>
    texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] std::optional<RenderGraphResolvedBuffer>
    buffer(RenderGraphBufferHandle handle) const;

  private:
    void allocate_transients(const cubey::vulkan::Device& device, const CompiledRenderGraph& graph);

    std::vector<std::optional<RenderGraphResolvedTexture>> textures_{};
    std::vector<std::optional<RenderGraphResolvedBuffer>> buffers_{};
    std::vector<cubey::vulkan::Image> transient_textures_{};
    std::vector<cubey::vulkan::Buffer> transient_buffers_{};
};

class RenderGraphFrameResources {
  public:
    RenderGraphFrameResources() = default;
    explicit RenderGraphFrameResources(std::uint32_t frame_slot_count);

    void resize(std::uint32_t frame_slot_count);
    void clear();

    [[nodiscard]] std::uint32_t frame_slot_count() const;

    RenderGraphResourceSet& emplace(FrameSlot slot, const CompiledRenderGraph& graph);
    RenderGraphResourceSet& emplace(FrameSlot slot, const cubey::vulkan::Device& device,
                                    const CompiledRenderGraph& graph);

    [[nodiscard]] RenderGraphResourceSet& resource_set(FrameSlot slot);
    [[nodiscard]] const RenderGraphResourceSet& resource_set(FrameSlot slot) const;

  private:
    void validate_slot(FrameSlot slot) const;

    std::vector<std::optional<RenderGraphResourceSet>> slots_{};
};

struct RenderGraphFrameRecordInfo {
    const cubey::vulkan::Device* device = nullptr;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    FrameSlot frame_slot{};
    const char* label = "vkEndCommandBuffer render graph";
};

using RenderGraphPrepareCallback = std::function<void(const RenderGraphResourceSet&)>;

class RenderGraphFrameExecutor {
  public:
    RenderGraphFrameExecutor() = default;
    explicit RenderGraphFrameExecutor(std::uint32_t frame_slot_count);

    void resize(std::uint32_t frame_slot_count);
    void clear();

    [[nodiscard]] std::uint32_t frame_slot_count() const;

    void record(const RenderGraphFrameRecordInfo& info, const CompiledRenderGraph& graph,
                RenderGraphPrepareCallback prepare = {});

  private:
    RenderGraphFrameResources resources_{};
};

class RenderGraphBuilder;

class RenderGraphPassBuilder {
  public:
    RenderGraphPassBuilder& read_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& read_storage_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& write_storage_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& read_write_storage_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& write_color(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& write_depth(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& transfer_read_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& transfer_write_texture(RenderGraphTextureHandle handle);

    RenderGraphPassBuilder& read_uniform_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& read_storage_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& write_storage_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& read_write_storage_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& transfer_read_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& transfer_write_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& material_pass(MaterialPassInfo info);
    RenderGraphPassBuilder& execute(RenderGraphExecuteCallback callback);

  private:
    friend class RenderGraphBuilder;

    RenderGraphPassBuilder(RenderGraphBuilder& graph, std::uint32_t pass_index);

    RenderGraphBuilder* graph_ = nullptr;
    std::uint32_t pass_index_ = 0;
};

class RenderGraphBuilder {
  public:
    [[nodiscard]] RenderGraphTextureHandle
    import_color_target(std::string label, ColorTargetView target,
                        std::optional<RenderGraphTextureState> initial_state = std::nullopt,
                        std::optional<RenderGraphTextureState> final_state = std::nullopt);
    [[nodiscard]] RenderGraphTextureHandle
    import_depth_target(std::string label, DepthTargetView target,
                        std::optional<RenderGraphTextureState> initial_state = std::nullopt,
                        std::optional<RenderGraphTextureState> final_state = std::nullopt);
    [[nodiscard]] RenderGraphTextureHandle
    import_texture(RenderGraphTextureDesc desc, VkImage image, VkImageView view,
                   std::optional<RenderGraphTextureState> initial_state = std::nullopt,
                   std::optional<RenderGraphTextureState> final_state = std::nullopt);
    [[nodiscard]] RenderGraphTextureHandle create_texture(RenderGraphTextureDesc desc);

    [[nodiscard]] RenderGraphBufferHandle
    import_buffer(RenderGraphBufferDesc desc, VkBuffer buffer,
                  std::optional<RenderGraphBufferState> initial_state = std::nullopt,
                  std::optional<RenderGraphBufferState> final_state = std::nullopt);
    [[nodiscard]] RenderGraphBufferHandle create_buffer(RenderGraphBufferDesc desc);

    [[nodiscard]] RenderGraphPassBuilder add_pass(std::string label, RenderGraphQueueDomain domain);
    [[nodiscard]] CompiledRenderGraph compile() const;

  private:
    friend class RenderGraphPassBuilder;

    void add_texture_access(std::uint32_t pass_index, RenderGraphTextureHandle handle,
                            RenderGraphTextureUsage usage);
    void add_buffer_access(std::uint32_t pass_index, RenderGraphBufferHandle handle,
                           RenderGraphBufferUsage usage);
    void set_material_pass(std::uint32_t pass_index, MaterialPassInfo info);
    void set_execute_callback(std::uint32_t pass_index, RenderGraphExecuteCallback callback);

    [[nodiscard]] const RenderGraphTextureResource&
    texture_resource(RenderGraphTextureHandle handle) const;
    [[nodiscard]] const RenderGraphBufferResource&
    buffer_resource(RenderGraphBufferHandle handle) const;

    std::vector<RenderGraphTextureResource> textures_{};
    std::vector<RenderGraphBufferResource> buffers_{};
    std::vector<RenderGraphCompiledPass> passes_{};
};

void record_render_graph_barriers(const cubey::vulkan::CommandRecorder& recorder,
                                  const RenderGraphExecutionContext& context,
                                  RenderGraphBarrierPhase phase);
[[nodiscard]] ColorTargetView resolved_color_target_view(const RenderGraphExecutionContext& context,
                                                         RenderGraphTextureHandle handle);
[[nodiscard]] RenderGraphSampledTextureView
resolved_sampled_texture_view(const RenderGraphExecutionContext& context,
                              RenderGraphTextureHandle handle);
[[nodiscard]] RenderGraphSampledTextureView
resolved_sampled_texture_view(const CompiledRenderGraph& graph,
                              const RenderGraphResourceSet& resources,
                              RenderGraphTextureHandle handle);

} // namespace cubey::render
