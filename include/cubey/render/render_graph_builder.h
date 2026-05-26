#pragma once

#include <cubey/render/render_graph_compiled.h>
#include <cubey/render/target.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cubey::render {

class RenderGraphBuilder;

class RenderGraphPassBuilder {
  public:
    RenderGraphPassBuilder& read_texture(RenderGraphTextureHandle handle,
                                         VkPipelineStageFlags stage_mask = 0);
    RenderGraphPassBuilder& read_storage_texture(RenderGraphTextureHandle handle,
                                                 VkPipelineStageFlags stage_mask = 0);
    RenderGraphPassBuilder& write_storage_texture(RenderGraphTextureHandle handle,
                                                  VkPipelineStageFlags stage_mask = 0);
    RenderGraphPassBuilder& read_write_storage_texture(RenderGraphTextureHandle handle,
                                                       VkPipelineStageFlags stage_mask = 0);
    RenderGraphPassBuilder& write_color(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& write_depth(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& transfer_read_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& transfer_write_texture(RenderGraphTextureHandle handle);

    RenderGraphPassBuilder& read_uniform_buffer(RenderGraphBufferHandle handle,
                                                VkPipelineStageFlags stage_mask = 0);
    RenderGraphPassBuilder& read_storage_buffer(RenderGraphBufferHandle handle,
                                                VkPipelineStageFlags stage_mask = 0);
    RenderGraphPassBuilder& write_storage_buffer(RenderGraphBufferHandle handle,
                                                 VkPipelineStageFlags stage_mask = 0);
    RenderGraphPassBuilder& read_write_storage_buffer(RenderGraphBufferHandle handle,
                                                      VkPipelineStageFlags stage_mask = 0);
    RenderGraphPassBuilder& read_vertex_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& read_index_buffer(RenderGraphBufferHandle handle);
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
                            RenderGraphTextureUsage usage, VkPipelineStageFlags stage_mask = 0);
    void add_buffer_access(std::uint32_t pass_index, RenderGraphBufferHandle handle,
                           RenderGraphBufferUsage usage, VkPipelineStageFlags stage_mask = 0);
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

} // namespace cubey::render
