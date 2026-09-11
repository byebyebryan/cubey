#include <cubey/animation/gltf_animation.h>
#include <cubey/engine/gltf_scene_importer.h>
#include <cubey/render/resource_registry.h>
#include <cubey/scene/scene.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan.h>

#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "source_file_test_helpers.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float value, float expected, const char* message) {
    constexpr float kTolerance = 0.0001F;
    if (value < expected - kTolerance || value > expected + kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_matrix_close(const cubey::math::Mat4& actual, const cubey::math::Mat4& expected,
                          const char* message) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            require_close(actual[column][row], expected[column][row], message);
        }
    }
}

} // namespace

void test_gltf_scene_importer_applies_rigid_animation_samples_to_imported_nodes() {
    cubey::asset::GltfAsset asset;
    asset.nodes.resize(2);
    asset.nodes[0].translation = {1.0F, 2.0F, 3.0F};
    asset.nodes[0].scale = {2.0F, 2.0F, 2.0F};
    asset.nodes[1].translation = {9.0F, 0.0F, 0.0F};

    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity animated = setup.entities().create();
    setup.transforms3d().create(animated, cubey::Transform3D{
                                              .translation = asset.nodes[0].translation,
                                              .scale = asset.nodes[0].scale,
                                          });
    const cubey::Entity untouched = setup.entities().create();
    setup.transforms3d().create(untouched, cubey::Transform3D{
                                               .translation = asset.nodes[1].translation,
                                           });
    setup.commit();

    cubey::GltfSceneImportResult import_result;
    import_result.node_entities = {animated, untouched};

    cubey::animation::GltfAnimationSample sample;
    sample.nodes.resize(2);
    sample.nodes[0].has_translation = true;
    sample.nodes[0].translation = {4.0F, 5.0F, 6.0F};
    sample.nodes[0].has_rotation = true;
    sample.nodes[0].rotation =
        cubey::math::angle_axis_quat(glm::half_pi<float>(), {0.0F, 1.0F, 0.0F});

    cubey::SceneEditQueue edits = scene.create_edit_queue();
    cubey::apply_gltf_rigid_animation_sample(edits, asset, import_result, sample);
    scene.commit(edits);

    cubey::SceneReadView view = scene.read();
    const cubey::Transform3D& animated_transform =
        view.transforms3d().local_transform(view.transforms3d().instance(animated));
    require_close(animated_transform.translation.x, 4.0F,
                  "sampled translation should apply to imported node");
    require_close(animated_transform.scale.x, 2.0F,
                  "unsampled scale should preserve base glTF node scale");
    require_close(animated_transform.rotation.y, sample.nodes[0].rotation.y,
                  "sampled rotation should apply to imported node");

    const cubey::Transform3D& untouched_transform =
        view.transforms3d().local_transform(view.transforms3d().instance(untouched));
    require_close(untouched_transform.translation.x, 9.0F,
                  "nodes without sampled TRS channels should remain untouched");
}

void test_gltf_scene_importer_preserves_matrix_nodes_and_animation_returns_to_trs() {
    cubey::math::Mat4 matrix{1.0F};
    matrix[0][0] = 2.0F;
    matrix[1][1] = 3.0F;
    matrix[2][2] = 4.0F;
    matrix[3][0] = 5.0F;
    matrix[3][1] = 6.0F;
    matrix[3][2] = 7.0F;

    cubey::asset::GltfNode node;
    node.has_matrix = true;
    node.local_matrix = matrix;
    node.translation = {1.0F, 2.0F, 3.0F};

    const cubey::Transform3D imported = cubey::gltf_node_transform_3d(node);
    require(imported.has_affine_matrix(), "matrix-authored glTF nodes should import as affine");
    require_matrix_close(imported.affine_matrix(), matrix,
                         "matrix-authored glTF nodes should preserve authored matrix");

    cubey::asset::GltfAsset asset;
    asset.nodes.resize(1);
    asset.nodes[0] = node;

    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity animated = setup.entities().create();
    setup.transforms3d().create(animated, imported);
    setup.commit();

    cubey::GltfSceneImportResult result;
    result.node_entities = {animated};

    cubey::animation::GltfAnimationSample sample;
    sample.nodes.resize(1);
    sample.nodes[0].has_translation = true;
    sample.nodes[0].translation = {8.0F, 9.0F, 10.0F};

    cubey::SceneEditQueue edits = scene.create_edit_queue();
    cubey::apply_gltf_rigid_animation_sample(edits, asset, result, sample);
    scene.commit(edits);

    cubey::SceneReadView view = scene.read();
    const cubey::Transform3D& animated_transform =
        view.transforms3d().local_transform(view.transforms3d().instance(animated));
    require(!animated_transform.has_affine_matrix(),
            "sampled TRS animation should clear matrix-authored affine override");
    require_close(animated_transform.translation.x, 8.0F,
                  "sampled TRS animation should apply sampled translation");
    require_close(animated_transform.scale.x, 1.0F,
                  "sampled TRS animation should preserve decomposed base scale");
}

void test_gltf_scene_importer_classifies_deformable_primitives() {
    cubey::asset::GltfNode static_node;
    cubey::asset::GltfNode skinned_node;
    skinned_node.skin_index = 0;
    cubey::asset::GltfMeshPrimitive static_primitive;
    cubey::asset::GltfMeshPrimitive morph_primitive;
    morph_primitive.morph_targets.resize(1);

    require(cubey::gltf_primitive_deformation_kind(static_node, static_primitive) ==
                cubey::GltfPrimitiveDeformationKind::Static,
            "static primitive should not require deformation");
    require(cubey::gltf_primitive_deformation_kind(static_node, morph_primitive) ==
                cubey::GltfPrimitiveDeformationKind::Morph,
            "morph targets should require morph deformation");
    require(cubey::gltf_primitive_deformation_kind(skinned_node, static_primitive) ==
                cubey::GltfPrimitiveDeformationKind::Skin,
            "skin node should require skin deformation");
    require(cubey::gltf_primitive_deformation_kind(skinned_node, morph_primitive) ==
                cubey::GltfPrimitiveDeformationKind::MorphSkin,
            "skinned morph primitive should require combined deformation");

    require(
        !cubey::gltf_primitive_requires_deformation(cubey::GltfPrimitiveDeformationKind::Static),
        "static primitive should not require deformation resources");
    require(cubey::gltf_primitive_requires_deformation(cubey::GltfPrimitiveDeformationKind::Skin),
            "skinned primitive should require deformation resources");
}

void test_gltf_scene_importer_prepares_owned_cpu_scene_without_engine_or_gpu() {
    cubey::asset::GltfAsset asset;
    asset.materials.emplace_back();
    asset.meshes.resize(1);
    cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives.emplace_back();
    primitive.vertices = {
        {.position = {1.0F, 2.0F, 3.0F}},
        {.position = {2.0F, 2.0F, 3.0F}},
        {.position = {1.0F, 3.0F, 3.0F}},
    };
    primitive.indices = {0, 1, 2};
    primitive.local_bounds = {
        .center = {1.5F, 2.5F, 3.0F},
        .half_extent = {0.5F, 0.5F, 0.0F},
    };
    asset.nodes = {{.mesh_index = 0}};
    asset.scenes = {{.root_nodes = {0}}};

    const cubey::GltfPreparedScene prepared = cubey::prepare_gltf_scene(
        asset, {.label_prefix = "prepared-test"}, {.supports_texture_compression_bc = false});

    require(prepared.materials.size() == 1,
            "CPU preparation should preserve material data without an engine");
    require(prepared.meshes.size() == 1 && prepared.meshes[0].primitives.size() == 1,
            "CPU preparation should convert mesh primitives without GPU residency");
    require(prepared.nodes.size() == 1 && prepared.root_nodes == std::vector<std::uint32_t>{0},
            "CPU preparation should resolve the selected node hierarchy");
    require(prepared.triangle_count == 1,
            "CPU preparation should report triangle metadata before GPU residency");
    require_close(prepared.bounds.center.x, 1.5F,
                  "CPU preparation should calculate scene bounds from prepared geometry");

    asset.meshes[0].primitives[0].vertices[0].position.x = 99.0F;
    require_close(prepared.meshes[0].primitives[0].vertices[0].position.x, 1.0F,
                  "prepared CPU data should own converted vertices independently of the asset");
}

void test_gltf_scene_importer_reserves_provisional_handle_generation() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MaterialHandle old_material = registry.create_material();
    const cubey::render::MaterialHandle replacement_material = registry.create_material();
    const cubey::render::MeshHandle old_mesh = registry.create_mesh();
    const cubey::render::MeshHandle replacement_mesh = registry.create_mesh();

    const cubey::render::MaterialHandle provisional_material{
        .index = replacement_material.index,
        .generation = 0,
    };
    const cubey::render::MeshHandle provisional_mesh{
        .index = replacement_mesh.index,
        .generation = 0,
    };
    const cubey::render::MaterialHandle legacy_material{
        .index = replacement_material.index,
        .generation = 1,
    };
    const cubey::render::MeshHandle legacy_mesh{
        .index = replacement_mesh.index,
        .generation = 1,
    };
    require(old_material.index == 1 && replacement_material.index == 2,
            "replacement fixture should keep an old material registry entry alive");
    require(old_mesh.index == 1 && replacement_mesh.index == 2,
            "replacement fixture should keep an old mesh registry entry alive");
    require(
        replacement_material == legacy_material,
        "the legacy generation-one provisional material identity should collide on replacement");
    require(replacement_mesh == legacy_mesh,
            "the legacy generation-one provisional mesh identity should collide on replacement");
    require(replacement_material != provisional_material,
            "replacement material registry handles must not collide with provisional handles");
    require(replacement_mesh != provisional_mesh,
            "replacement mesh registry handles must not collide with provisional handles");

    const std::filesystem::path root = CUBEY_SOURCE_DIR;
    const std::string session =
        cubey::tests::read_source_file(root / "src/cubey/engine/gltf_scene_upload_session.cpp");
    cubey::tests::require_contains(
        session, "staging_mesh_handle(std::size_t index)",
        "glTF upload session should construct explicit provisional mesh handles");
    cubey::tests::require_contains(
        session, "staging_material_handle(std::size_t index)",
        "glTF upload session should construct explicit provisional material handles");
    cubey::tests::require_contains(
        session, ".generation = 0U};",
        "glTF upload session should reserve generation zero for provisional handles");
}

void test_gltf_scene_importer_validates_deformation_inputs_and_culling_policy() {
    const std::filesystem::path root = CUBEY_SOURCE_DIR;
    const std::string deformation = cubey::tests::read_source_file(
        root / "src/cubey/engine/gltf_scene_importer_deformation.cpp");
    const std::string importer =
        cubey::tests::read_source_file(root / "src/cubey/engine/gltf_scene_importer.cpp");

    cubey::tests::require_contains(
        deformation, "validate_skin_influences",
        "glTF deformation import should reject unusable joint/weight data before upload");
    cubey::tests::require_contains(
        deformation, "morph weight count must match primitive morph target count",
        "glTF deformation import should reject malformed morph weight arrays");
    cubey::tests::require_contains(
        importer, ".culling_enabled = !has_deformable_primitive",
        "glTF deformable renderables should opt out of static bounds frustum culling");
}

void test_gltf_scene_upload_policy_rejects_invalid_bounds() {
    const auto prepared = std::make_shared<cubey::GltfPreparedScene>();
    cubey::vulkan::SubmissionCoordinator submission(
        reinterpret_cast<VkQueue>(0x661),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [](VkQueue, const char*) {});
    cubey::vulkan::GpuRuntime gpu({
        .device = reinterpret_cast<cubey::vulkan::Device*>(0x662),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    const cubey::GltfSceneUploadPolicy defaults;
    require(defaults.owner_cpu_target_milliseconds == 2.0 &&
                defaults.step_byte_cap == 32ULL * 1024ULL * 1024ULL &&
                defaults.copy_byte_target == 2ULL * 1024ULL * 1024ULL,
            "glTF upload policy should preserve the reviewed default tuning");

    cubey::GltfSceneImportConfig config;
    config.upload_policy.owner_cpu_target_milliseconds = std::numeric_limits<double>::infinity();
    bool rejected_target = false;
    try {
        static_cast<void>(cubey::begin_gltf_scene_upload_session(prepared, config, gpu));
    } catch (const std::runtime_error&) {
        rejected_target = true;
    }
    require(rejected_target, "glTF upload policy should reject a non-finite owner target");

    config = {};
    config.upload_policy.step_byte_cap = 0U;
    bool rejected_step_cap = false;
    try {
        static_cast<void>(cubey::begin_gltf_scene_upload_session(prepared, config, gpu));
    } catch (const std::runtime_error&) {
        rejected_step_cap = true;
    }
    require(rejected_step_cap, "glTF upload policy should reject a zero step cap");

    config = {};
    config.upload_policy.copy_byte_target = 0U;
    bool rejected_copy_target = false;
    try {
        static_cast<void>(cubey::begin_gltf_scene_upload_session(prepared, config, gpu));
    } catch (const std::runtime_error&) {
        rejected_copy_target = true;
    }
    require(rejected_copy_target, "glTF upload policy should reject a zero copy target");

    config = {};
    config.upload_policy.copy_byte_target = config.upload_policy.step_byte_cap + 1U;
    bool rejected_incompatible_caps = false;
    try {
        static_cast<void>(cubey::begin_gltf_scene_upload_session(prepared, config, gpu));
    } catch (const std::runtime_error&) {
        rejected_incompatible_caps = true;
    }
    require(rejected_incompatible_caps,
            "glTF upload policy should reject copy targets larger than a physical step");

    bool rejected_missing_pool = false;
    try {
        static_cast<void>(cubey::begin_gltf_scene_upload_session(prepared, {}, gpu));
    } catch (const std::runtime_error& error) {
        rejected_missing_pool = std::string(error.what()) ==
                                "glTF upload session requires a configured GPU staging pool";
    }
    require(rejected_missing_pool,
            "glTF upload sessions should diagnose a missing configured staging pool");
}

void test_gltf_scene_importer_blocking_path_uses_upload_session() {
    const std::filesystem::path root = CUBEY_SOURCE_DIR;
    const std::string importer =
        cubey::tests::read_source_file(root / "src/cubey/engine/gltf_scene_importer.cpp");
    const std::string session =
        cubey::tests::read_source_file(root / "src/cubey/engine/gltf_scene_upload_session.cpp");
    cubey::tests::require_contains(importer,
                                   "begin_gltf_scene_upload_session(prepared, config, gpu)",
                                   "blocking glTF import should create the shared upload session");
    cubey::tests::require_contains(
        importer, "session->poll(gpu, true)",
        "blocking glTF import should advance the session through its wait path");
    cubey::tests::require_contains(
        importer, "session->take_resident()",
        "blocking glTF import should activate only the completed session resident");
    cubey::tests::require_contains(
        session, "retire abandoned glTF upload session",
        "abandoned glTF sessions should retain a runtime-owned cleanup registration");
    cubey::tests::require_contains(
        session, "register_owner_cleanup",
        "abandoned glTF sessions should register owner-only retirement with their runtime");
    cubey::tests::require_contains(
        session, "owner.defer_destruction_after(ticket->submission_ticket()",
        "abandoned glTF sessions should retire after their final upload ticket");
    cubey::tests::require_contains(
        session, "if (abandoned || phase == Phase::Failed",
        "abandoned glTF sessions should stop issuing later owner advances");
    cubey::tests::require_contains(
        session, "std::shared_ptr<Impl::QueuedOwnerStep>",
        "waiting polls should retain a stable owner-job handle outside the session mutex");
    cubey::tests::require_contains(
        session, "owner_cleanup.reset()",
        "successful resident adoption should unregister the runtime cleanup action");
}
