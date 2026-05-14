#include <cubey/animation/gltf_animation.h>
#include <cubey/engine/gltf_scene_importer.h>
#include <cubey/scene/scene.h>

#include <glm/gtc/constants.hpp>

#include <stdexcept>

namespace {

void require_close(float value, float expected, const char* message) {
    constexpr float kTolerance = 0.0001F;
    if (value < expected - kTolerance || value > expected + kTolerance) {
        throw std::runtime_error(message);
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
