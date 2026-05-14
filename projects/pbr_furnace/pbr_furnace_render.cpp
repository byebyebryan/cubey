#include "pbr_furnace_app_internal.h"

#include <cubey/render/pass.h>
#include <cubey/scene/render_recording.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>

#include <array>

namespace cubey::projects::pbr_furnace {

void PbrFurnaceApp::create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                                        VkFormat color_format) {
    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::vertex_shader_file(shader_path("pbr_furnace.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("pbr_furnace.frag.spv")),
    };
    const cubey::render::VertexInputLayout vertex_input = cubey::render::pbr_vertex_input_layout();
    const std::array<VkDescriptorSetLayout, 2> set_layouts{
        scene_material().layout(),
        material_instances_.at(material_handles_.front()).layout(),
    };
    forward_pass_.emplace(
        device,
        cubey::render::GraphicsPipelineTargetInfo{
            .extent = extent,
            .color_format = color_format,
        },
        cubey::render::ForwardScenePass3DConfig{
            .pipeline =
                {
                    .shader_stage_files = shader_stage_files,
                    .vertex_bindings = vertex_input.bindings(),
                    .vertex_attributes = vertex_input.attribute_descriptions(),
                    .descriptor_set_layouts = set_layouts,
                    .material_pass = cubey::render::pbr_forward_pass_info(),
                },
            .clear =
                {
                    .color = cubey::render::color_clear_value(0.018F, 0.018F, 0.018F, 1.0F),
                    .depth = cubey::render::depth_clear_value(),
                },
        });
}

void PbrFurnaceApp::destroy_swapchain_resources() {
    forward_pass_.reset();
}

void PbrFurnaceApp::destroy_all_resources() {
    destroy_swapchain_resources();
    scene_material_.reset();
    destroy_scene_if_needed();
    destroy_material_resources();
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, sphere_mesh_handle_);
    white_environment_.reset();
    dummy_shadow_.reset();
    normal_default_.reset();
    metallic_roughness_default_.reset();
    emissive_default_.reset();
    occlusion_default_.reset();
    base_color_default_.reset();
}

void PbrFurnaceApp::record_furnace_frame(VkCommandBuffer command_buffer,
                                         cubey::render::ColorTargetView color_target,
                                         cubey::render::FrameSlot frame_slot, bool present) {
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::RenderFramePlan3D frame_plan =
        current_frame_plan(scene_view, color_target.extent);
    scene_material().upload(frame_slot,
                            scene_uniforms(scene_view, frame_plan, color_target.format));

    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    if (present) {
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    }
    const auto record = [this, &frame_plan,
                         frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
        cubey::scene::record_pipeline_draw_packets_3d(
            pass_recorder, frame_plan.draw_packets, meshes_,
            {
                .pipeline = &forward_pass().pipeline(),
                .material = &scene_material().material(),
                .frame_slot = frame_slot,
                .filter =
                    {
                        .material_pass = cubey::render::MaterialPassKind::ForwardColor,
                        .blend_mode = cubey::render::MaterialBlendMode::Opaque,
                    },
            },
            [this, frame_slot](const cubey::vulkan::CommandRecorder& packet_recorder,
                               const cubey::scene::RenderDrawPacket3D& packet) {
                const auto& material = material_instances_.at(packet.material);
                material.upload(frame_slot, cubey::render::pbr_material_uniforms(
                                                material_factors_.at(packet.material)));
                cubey::render::bind_material_instance(packet_recorder, forward_pass().pipeline(),
                                                      material.material(), frame_slot);
                packet_recorder.push_constants(
                    forward_pass().pipeline().layout(),
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    cubey::render::pbr_push_constants(packet.world_affine_matrix));
            });
    };
    if (present) {
        forward_pass().record_to_present_target(recorder, color_target, record);
        recorder.end("vkEndCommandBuffer pbr_furnace");
    } else {
        recorder.transition_image_layout(cubey::vulkan::begin_depth_attachment_transition(
            forward_pass().depth_attachment().handle()));
        forward_pass().record_to_prepared_target(recorder, color_target, record);
    }
}

} // namespace cubey::projects::pbr_furnace
