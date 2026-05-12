#include <cubey/scene/light_manager.h>
#include <cubey/core/math.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>

#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (std::fabs(actual - expected) > kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_throws(const std::function<void()>& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

const cubey::LightPacket3D& packet_for(const std::vector<cubey::LightPacket3D>& packets,
                                       cubey::Entity entity) {
    for (const cubey::LightPacket3D& packet : packets) {
        if (packet.entity == entity) {
            return packet;
        }
    }
    throw std::runtime_error("expected light packet was not emitted");
}

void require_invalid_light_rejected(const cubey::Light3D& light, const char* message) {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.lights3d().create(entity, light);
    require_throws([&setup] { setup.commit(); }, message);
}

} // namespace

void test_light_manager_publishes_3d_packets_from_scene_read_view() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity directional_entity = setup.entities().create();
    const cubey::Entity point_entity = setup.entities().create();
    setup.lights3d().create(directional_entity, cubey::directional_light_3d(
                                                    {0.0F, -2.0F, 0.0F}, {0.8F, 0.7F, 0.6F}, 2.0F));
    setup.transforms3d().create(point_entity,
                                cubey::Transform3D{.translation = {1.0F, 2.0F, 3.0F}});
    setup.lights3d().create(point_entity, cubey::point_light_3d({0.2F, 0.3F, 0.4F}, 5.0F, 8.0F));
    setup.commit();

    cubey::SceneReadView view = scene.read();
    const cubey::LightInstance3D directional_instance =
        view.lights3d().instance(directional_entity);
    const cubey::Light3D& directional = view.lights3d().light(directional_instance);
    require(directional.kind == cubey::LightKind3D::Directional,
            "read view should publish directional light kind");
    require_close(directional.intensity, 2.0F, "read view should publish light intensity");
    require(view.lights3d().active_instances().size() == 2,
            "read view should publish all active lights");

    const std::vector<cubey::LightPacket3D> packets =
        cubey::build_light_packets_3d(view.lights3d(), view.transforms3d());
    require(packets.size() == 2, "visible lights should produce packets");

    const cubey::LightPacket3D& directional_packet = packet_for(packets, directional_entity);
    require(directional_packet.kind == cubey::LightKind3D::Directional,
            "directional packet should carry light kind");
    require_close(directional_packet.direction.x, 0.0F,
                  "directional packet should normalize direction x");
    require_close(directional_packet.direction.y, -1.0F,
                  "directional packet should normalize direction y");
    require_close(directional_packet.direction.z, 0.0F,
                  "directional packet should normalize direction z");
    require_close(directional_packet.color.x, 0.8F, "directional packet should carry color");
    require_close(directional_packet.intensity, 2.0F, "directional packet should carry intensity");

    const cubey::LightPacket3D& point_packet = packet_for(packets, point_entity);
    require(point_packet.kind == cubey::LightKind3D::Point, "point packet should carry light kind");
    require_close(point_packet.position.x, 1.0F, "point packet should carry world position x");
    require_close(point_packet.position.y, 2.0F, "point packet should carry world position y");
    require_close(point_packet.position.z, 3.0F, "point packet should carry world position z");
    require_close(point_packet.range, 8.0F, "point packet should carry range");
    require_close(point_packet.intensity, 5.0F, "point packet should carry intensity");
}

void test_light_manager_updates_keep_epoch_local_snapshots() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.lights3d().create(
        entity, cubey::directional_light_3d({0.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F));
    setup.commit();

    cubey::SceneReadView before = scene.read();
    cubey::SceneEditQueue edits = scene.create_edit_queue();
    edits.lights3d().set_light(
        entity, cubey::directional_light_3d({1.0F, 0.0F, 0.0F}, {0.1F, 0.2F, 0.3F}, 3.0F));
    scene.commit(edits);
    cubey::SceneReadView after = scene.read();

    const cubey::LightInstance3D before_light = before.lights3d().instance(entity);
    const cubey::LightInstance3D after_light = after.lights3d().instance(entity);
    require_close(before.lights3d().light(before_light).color.x, 1.0F,
                  "older read view should keep previous light state");
    require_close(after.lights3d().light(after_light).color.x, 0.1F,
                  "new read view should publish updated light state");
    require_close(after.lights3d().light(after_light).intensity, 3.0F,
                  "new read view should publish updated light intensity");
}

void test_light_manager_entity_destroy_retires_attached_components() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.lights3d().create(
        entity, cubey::directional_light_3d({0.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F));
    setup.commit();

    cubey::SceneReadView before = scene.read();
    cubey::SceneEditQueue edits = scene.create_edit_queue();
    edits.destroy(entity);
    scene.commit(edits);
    cubey::SceneReadView after = scene.read();

    const std::vector<cubey::LightPacket3D> old_packets =
        cubey::build_light_packets_3d(before.lights3d(), before.transforms3d());
    require(old_packets.size() == 1, "older read view should keep destroyed light snapshot");
    require_throws([&after, entity] { (void)after.lights3d().instance(entity); },
                   "new read view should not expose light for destroyed entity");
}

void test_light_manager_rejects_invalid_edits() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.lights3d().create(
        entity, cubey::directional_light_3d({0.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F));
    setup.commit();

    cubey::SceneEditQueue duplicate = scene.create_edit_queue();
    duplicate.lights3d().create(entity, cubey::directional_light_3d({0.0F, -1.0F, 0.0F}));
    require_throws([&scene, &duplicate] { scene.commit(duplicate); },
                   "light manager should reject duplicate components");

    cubey::SceneTransaction missing_setup = scene.begin_transaction();
    const cubey::Entity missing_component = missing_setup.entities().create();
    missing_setup.commit();
    cubey::SceneEditQueue missing_update = scene.create_edit_queue();
    missing_update.lights3d().set_light(missing_component,
                                        cubey::directional_light_3d({0.0F, -1.0F, 0.0F}));
    require_throws([&scene, &missing_update] { scene.commit(missing_update); },
                   "light manager should reject updates without a component");

    cubey::SceneTransaction stale_setup = scene.begin_transaction();
    const cubey::Entity stale_entity = stale_setup.entities().create();
    stale_setup.commit();
    cubey::SceneEditQueue stale_destroy = scene.create_edit_queue();
    stale_destroy.destroy(stale_entity);
    scene.commit(stale_destroy);
    cubey::SceneEditQueue stale_create = scene.create_edit_queue();
    stale_create.lights3d().create(stale_entity, cubey::directional_light_3d({0.0F, -1.0F, 0.0F}));
    require_throws([&scene, &stale_create] { scene.commit(stale_create); },
                   "light manager should reject stale entity handles");

    require_invalid_light_rejected(cubey::directional_light_3d({0.0F, 0.0F, 0.0F}),
                                   "light manager should reject zero directional vectors");
    require_invalid_light_rejected(
        cubey::directional_light_3d({0.0F, -1.0F, 0.0F}, {-1.0F, 1.0F, 1.0F}),
        "light manager should reject negative color channels");
    require_invalid_light_rejected(
        cubey::directional_light_3d({0.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, -1.0F),
        "light manager should reject negative intensity");
    require_invalid_light_rejected(cubey::point_light_3d({1.0F, 1.0F, 1.0F}, 1.0F, 0.0F),
                                   "light manager should reject non-positive point range");
}

void test_light_manager_skips_invisible_lights_without_transform() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    cubey::Light3D light = cubey::point_light_3d({1.0F, 1.0F, 1.0F}, 1.0F, 4.0F);
    light.visible = false;
    setup.lights3d().create(entity, light);
    setup.commit();

    cubey::SceneReadView view = scene.read();
    const std::vector<cubey::LightPacket3D> packets =
        cubey::build_light_packets_3d(view.lights3d(), view.transforms3d());
    require(packets.empty(), "invisible lights should not require a transform or packet");
}

void test_light_packets_require_transform_for_point_lights() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.lights3d().create(entity, cubey::point_light_3d({1.0F, 1.0F, 1.0F}, 1.0F, 4.0F));
    setup.commit();

    cubey::SceneReadView view = scene.read();
    require_throws(
        [&view] { (void)cubey::build_light_packets_3d(view.lights3d(), view.transforms3d()); },
        "visible point lights should require a 3d transform");
}
