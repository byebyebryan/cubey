#include "shadow_cube_app_internal.h"
#include "shadow_cube_render.h"

#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>

#include <array>

namespace cubey::examples::shadow_cube::detail {
namespace {

constexpr std::array<cubey::render::PrimitiveVec3, 6> kCubeFaceColors{
    cubey::render::PrimitiveVec3{0.88F, 0.35F, 0.26F},
    cubey::render::PrimitiveVec3{0.24F, 0.58F, 0.86F},
    cubey::render::PrimitiveVec3{0.32F, 0.68F, 0.38F},
    cubey::render::PrimitiveVec3{0.92F, 0.70F, 0.22F},
    cubey::render::PrimitiveVec3{0.66F, 0.44F, 0.86F},
    cubey::render::PrimitiveVec3{0.30F, 0.72F, 0.74F},
};

constexpr cubey::render::PrimitiveVec3 kFloorColor{0.58F, 0.58F, 0.52F};

} // namespace

void ShadowCubeApp::create_global_resources_if_needed(cubey::host::WindowedAppContext& context) {
    if (scene_ != nullptr) {
        return;
    }

    material_handle_ = engine_.render_resources().create_material("shadow_cube.material");
    cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, context.gpu(), "shadow_cube.cube",
        cubey::render::make_cube_position_color_normal_mesh({
            .face_colors = kCubeFaceColors,
        }));
    floor_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, context.gpu(), "shadow_cube.floor",
        cubey::render::make_xz_plane_position_color_normal_mesh({
            .center = {0.0F, -1.05F, 0.0F},
            .half_extent_x = 4.0F,
            .half_extent_z = 4.0F,
            .color = kFloorColor,
        }));
    create_scene();
    create_shadow_depth_resources(context);
    create_descriptors(context);
}

void ShadowCubeApp::create_swapchain_resources(cubey::host::WindowedAppContext& context) {
    depth_attachment_.emplace(context.device(), context.swapchain().extent());
    graph_executor_.clear();
    graph_executor_.resize(context.frame_slot_count());
    create_present_resources(context);
    create_pipelines(context);
}

void ShadowCubeApp::destroy_swapchain_resources() {
    graph_executor_.clear();
    present_pipeline_resource_.reset();
    present_material_instance_.reset();
    present_sampler_.reset();
    scene_pipeline_resource_.reset();
    shadow_pipeline_resource_.reset();
    depth_attachment_.reset();
}

void ShadowCubeApp::destroy_all_resources() {
    destroy_swapchain_resources();
    scene_material_instance_.reset();
    shadow_depth_.reset();
    shadow_depth_is_sampled_ = false;
    destroy_scene_if_needed();
    destroy_render_handles();
}

void ShadowCubeApp::create_shadow_depth_resources(cubey::host::WindowedAppContext& context) {
    const VkFormat shadow_format = cubey::vulkan::choose_depth_format(context.device());
    shadow_depth_.emplace(context.device(),
                          cubey::render::DepthTextureConfig{
                              .extent = {kShadowMapSize, kShadowMapSize},
                              .format = shadow_format,
                              .create_sampler = true,
                              .sampler =
                                  {
                                      .min_filter = VK_FILTER_NEAREST,
                                      .mag_filter = VK_FILTER_NEAREST,
                                      .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                      .border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                                  },
                          });
}

void ShadowCubeApp::create_descriptors(cubey::host::WindowedAppContext& context) {
    scene_material_instance_.emplace(context.device(),
                                     cubey::render::MaterialInstanceConfig{
                                         .material_pass = shadow_scene_pass_info(),
                                         .descriptor_set = 0,
                                     });
    cubey::render::MaterialDescriptorWriter(scene_material_instance().set())
        .combined_image_sampler(0, shadow_depth().sampler().handle(), shadow_depth().view(),
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        .update(context.device());
}

void ShadowCubeApp::create_present_resources(cubey::host::WindowedAppContext& context) {
    present_sampler_.emplace(context.device(),
                             cubey::vulkan::SamplerConfig{
                                 .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             });
    present_material_instance_.emplace(context.device(),
                                       cubey::render::MaterialInstanceConfig{
                                           .material_pass = shadow_present_pass_info(),
                                           .descriptor_set = 0,
                                           .set_count = context.frame_slot_count(),
                                       });
}

void ShadowCubeApp::create_pipelines(cubey::host::WindowedAppContext& context) {
    create_shadow_pipeline(context);
    create_scene_pipeline(context);
    create_present_pipeline(context);
}

void ShadowCubeApp::create_shadow_pipeline(cubey::host::WindowedAppContext& context) {
    const std::array<cubey::render::ShaderStageFile, 1> shader_stage_files{
        cubey::render::vertex_shader_file(shader_path("shadow_depth.vert.spv")),
    };
    const cubey::render::VertexInputLayout vertex_input =
        cubey::render::vertex_position_only_input_layout(
            sizeof(cubey::render::VertexPositionColorNormal));
    shadow_pipeline_resource_.emplace(
        context.device(), cubey::render::graphics_pipeline_file_resource_config(
                              {
                                  .extent = shadow_depth().extent(),
                                  .depth_format = shadow_depth().format(),
                              },
                              {
                                  .shader_stage_files = shader_stage_files,
                                  .vertex_bindings = vertex_input.bindings(),
                                  .vertex_attributes = vertex_input.attribute_descriptions(),
                                  .material_pass = shadow_depth_pass_info(),
                              }));
}

void ShadowCubeApp::create_scene_pipeline(cubey::host::WindowedAppContext& context) {
    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::vertex_shader_file(shader_path("shadow_cube.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("shadow_cube.frag.spv")),
    };
    const cubey::render::VertexInputLayout vertex_input =
        cubey::render::vertex_position_color_normal_input_layout();
    const std::array<VkDescriptorSetLayout, 1> set_layouts{scene_material_instance().layout()};
    scene_pipeline_resource_.emplace(
        context.device(), cubey::render::graphics_pipeline_file_resource_config(
                              {
                                  .extent = context.swapchain().extent(),
                                  .color_format = context.swapchain().format(),
                                  .depth_format = depth_attachment().format(),
                              },
                              {
                                  .shader_stage_files = shader_stage_files,
                                  .vertex_bindings = vertex_input.bindings(),
                                  .vertex_attributes = vertex_input.attribute_descriptions(),
                                  .descriptor_set_layouts = set_layouts,
                                  .material_pass = shadow_scene_pass_info(),
                              }));
}

void ShadowCubeApp::create_present_pipeline(cubey::host::WindowedAppContext& context) {
    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::vertex_shader_file(shader_path("shadow_present.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("shadow_present.frag.spv")),
    };
    const std::array<VkDescriptorSetLayout, 1> set_layouts{present_material_instance().layout()};
    present_pipeline_resource_.emplace(context.device(),
                                       cubey::render::graphics_pipeline_file_resource_config(
                                           {
                                               .extent = context.swapchain().extent(),
                                               .color_format = context.swapchain().format(),
                                           },
                                           {
                                               .shader_stage_files = shader_stage_files,
                                               .descriptor_set_layouts = set_layouts,
                                               .material_pass = shadow_present_pass_info(),
                                           }));
}

} // namespace cubey::examples::shadow_cube::detail
