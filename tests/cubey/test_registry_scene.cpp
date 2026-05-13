#include "test_registry_common.h"

void test_camera_2d_clamps_scale();
void test_camera_2d_pans_zooms_and_reports_view();
void test_camera_3d_builds_projection_and_view_from_world_transform();
void test_camera_3d_orbit_helper_matches_existing_cube_view();
void test_camera_3d_supports_orthographic_projection();
void test_camera_manager_entity_destroy_retires_attached_components();
void test_camera_manager_rejects_duplicate_and_stale_entity_edits();
void test_camera_manager_updates_keep_epoch_local_snapshots();
void test_camera_managers_publish_2d_and_3d_camera_snapshots();
void test_entity_handles_track_null_reserved_and_alive_states();
void test_entity_manager_concurrent_reservations_are_unique();
void test_entity_manager_invalidates_generations_and_defers_reuse();
void test_entity_manager_rolls_back_reserved_entities();
void test_light_manager_entity_destroy_retires_attached_components();
void test_light_manager_publishes_3d_packets_from_scene_read_view();
void test_light_manager_rejects_invalid_edits();
void test_light_manager_skips_invisible_lights_without_transform();
void test_light_manager_updates_keep_epoch_local_snapshots();
void test_light_packets_require_transform_for_point_lights();
void test_render_plan_builds_sorted_3d_draw_packets_with_material_metadata();
void test_render_plan_converts_draw_packets_to_render_items();
void test_render_plan_filters_draw_packets_for_recording_policy();
void test_render_plan_rejects_stale_resource_handles();
void test_render_recording_rejects_ambiguous_material_binding_sources();
void test_render_view_3d_builds_frame_plan_with_environment_draws_and_lights();
void test_render_view_3d_builds_multiple_view_plans_from_one_scene_read_view();
void test_render_view_3d_frame_pass_plan_preserves_explicit_pass_order();
void test_render_view_3d_frustum_culls_world_bounds_and_can_be_disabled();
void test_render_view_3d_preserves_draw_sorting_and_stale_handle_validation();
void test_render_view_3d_rejects_invalid_view_or_missing_camera_transform();
void test_renderable_manager_emits_multiple_visible_primitives();
void test_renderable_manager_entity_destroy_retires_attached_components();
void test_renderable_manager_publishes_3d_packets_from_scene_read_view();
void test_renderable_manager_rejects_invalid_edits_and_missing_transforms();
void test_renderable_manager_skips_invisible_renderables_without_transform();
void test_renderable_manager_updates_keep_epoch_local_snapshots();
void test_scene_builder_creates_common_3d_entities();
void test_scene_edit_queue_publishes_reserved_entities_on_commit();
void test_scene_failed_commit_rolls_back_reserved_entities();
void test_scene_read_views_defer_destroyed_entity_reuse_until_release();
void test_stable_slot_store_rejects_stale_handles_without_moving_other_slots();
void test_transform_2d_builds_affine_matrix();
void test_transform_3d_builds_affine_matrix();
void test_transform_3d_matches_existing_cube_rotation_order();
void test_transform_manager_2d_publishes_parented_world_matrices();
void test_transform_manager_3d_publishes_parented_world_matrices();
void test_transform_manager_read_views_keep_epoch_local_snapshots();
void test_transform_manager_rejects_invalid_parenting_and_child_destroy();
void test_transform_manager_reparenting_preserves_local_transform();

namespace cubey::tests {

std::span<const TestCase> scene_test_cases() {
    static constexpr std::array tests{
        CUBEY_TEST(test_camera_2d_pans_zooms_and_reports_view),
        CUBEY_TEST(test_camera_2d_clamps_scale),
        CUBEY_TEST(test_camera_3d_builds_projection_and_view_from_world_transform),
        CUBEY_TEST(test_camera_3d_supports_orthographic_projection),
        CUBEY_TEST(test_camera_3d_orbit_helper_matches_existing_cube_view),
        CUBEY_TEST(test_camera_managers_publish_2d_and_3d_camera_snapshots),
        CUBEY_TEST(test_camera_manager_updates_keep_epoch_local_snapshots),
        CUBEY_TEST(test_camera_manager_entity_destroy_retires_attached_components),
        CUBEY_TEST(test_camera_manager_rejects_duplicate_and_stale_entity_edits),
        CUBEY_TEST(test_entity_handles_track_null_reserved_and_alive_states),
        CUBEY_TEST(test_entity_manager_invalidates_generations_and_defers_reuse),
        CUBEY_TEST(test_entity_manager_rolls_back_reserved_entities),
        CUBEY_TEST(test_entity_manager_concurrent_reservations_are_unique),
        CUBEY_TEST(test_light_manager_publishes_3d_packets_from_scene_read_view),
        CUBEY_TEST(test_light_manager_updates_keep_epoch_local_snapshots),
        CUBEY_TEST(test_light_manager_entity_destroy_retires_attached_components),
        CUBEY_TEST(test_light_manager_rejects_invalid_edits),
        CUBEY_TEST(test_light_manager_skips_invisible_lights_without_transform),
        CUBEY_TEST(test_light_packets_require_transform_for_point_lights),
        CUBEY_TEST(test_render_plan_builds_sorted_3d_draw_packets_with_material_metadata),
        CUBEY_TEST(test_render_plan_converts_draw_packets_to_render_items),
        CUBEY_TEST(test_render_plan_filters_draw_packets_for_recording_policy),
        CUBEY_TEST(test_render_recording_rejects_ambiguous_material_binding_sources),
        CUBEY_TEST(test_render_plan_rejects_stale_resource_handles),
        CUBEY_TEST(test_render_view_3d_builds_frame_plan_with_environment_draws_and_lights),
        CUBEY_TEST(test_render_view_3d_builds_multiple_view_plans_from_one_scene_read_view),
        CUBEY_TEST(test_render_view_3d_frame_pass_plan_preserves_explicit_pass_order),
        CUBEY_TEST(test_render_view_3d_rejects_invalid_view_or_missing_camera_transform),
        CUBEY_TEST(test_render_view_3d_frustum_culls_world_bounds_and_can_be_disabled),
        CUBEY_TEST(test_render_view_3d_preserves_draw_sorting_and_stale_handle_validation),
        CUBEY_TEST(test_renderable_manager_publishes_3d_packets_from_scene_read_view),
        CUBEY_TEST(test_renderable_manager_emits_multiple_visible_primitives),
        CUBEY_TEST(test_renderable_manager_skips_invisible_renderables_without_transform),
        CUBEY_TEST(test_renderable_manager_updates_keep_epoch_local_snapshots),
        CUBEY_TEST(test_renderable_manager_entity_destroy_retires_attached_components),
        CUBEY_TEST(test_renderable_manager_rejects_invalid_edits_and_missing_transforms),
        CUBEY_TEST(test_stable_slot_store_rejects_stale_handles_without_moving_other_slots),
        CUBEY_TEST(test_scene_builder_creates_common_3d_entities),
        CUBEY_TEST(test_scene_edit_queue_publishes_reserved_entities_on_commit),
        CUBEY_TEST(test_scene_failed_commit_rolls_back_reserved_entities),
        CUBEY_TEST(test_scene_read_views_defer_destroyed_entity_reuse_until_release),
        CUBEY_TEST(test_transform_manager_2d_publishes_parented_world_matrices),
        CUBEY_TEST(test_transform_manager_3d_publishes_parented_world_matrices),
        CUBEY_TEST(test_transform_manager_reparenting_preserves_local_transform),
        CUBEY_TEST(test_transform_manager_read_views_keep_epoch_local_snapshots),
        CUBEY_TEST(test_transform_manager_rejects_invalid_parenting_and_child_destroy),
        CUBEY_TEST(test_transform_2d_builds_affine_matrix),
        CUBEY_TEST(test_transform_3d_builds_affine_matrix),
        CUBEY_TEST(test_transform_3d_matches_existing_cube_rotation_order),
    };
    return tests;
}

} // namespace cubey::tests
