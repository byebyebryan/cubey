#include <cubey/math.h>
#include <cubey/transform_2d.h>
#include <cubey/transform_3d.h>
#include <cubey/transform_hierarchy.h>

#include <cmath>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>

namespace {

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (std::fabs(actual - expected) > kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_true(bool condition, const char* message) {
    if (!condition) {
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

} // namespace

void test_transform_hierarchy_2d_computes_lazy_world_affine_matrices() {
    cubey::TransformHierarchy2D hierarchy;
    const cubey::TransformNodeId root = hierarchy.create_node(cubey::Transform2D{
        .translation = {2.0F, 0.0F},
    });
    const cubey::TransformNodeId child = hierarchy.create_node(cubey::Transform2D{
        .translation = {3.0F, 0.0F},
    });
    hierarchy.set_parent(child, root);

    const cubey::math::Vec3 first_world =
        hierarchy.world_affine_matrix(child) * cubey::math::Vec3{0.0F, 0.0F, 1.0F};
    require_close(first_world.x, 5.0F, "2D child should inherit parent translation");
    require_close(first_world.y, 0.0F, "2D child should preserve local y");

    hierarchy.set_local_transform(root, cubey::Transform2D{
                                            .translation = {4.0F, 0.0F},
                                        });

    const cubey::math::Vec3 dirty_world =
        hierarchy.world_affine_matrix(child) * cubey::math::Vec3{0.0F, 0.0F, 1.0F};
    require_close(dirty_world.x, 7.0F, "2D child should update lazily after parent edit");
    require_close(dirty_world.y, 0.0F, "2D child y should remain stable after parent edit");
}

void test_transform_hierarchy_3d_updates_world_affine_matrices() {
    cubey::TransformHierarchy3D hierarchy;
    const cubey::TransformNodeId root = hierarchy.create_node(cubey::Transform3D{
        .translation = {1.0F, 2.0F, 3.0F},
    });
    const cubey::TransformNodeId child = hierarchy.create_node(cubey::Transform3D{
        .translation = {0.0F, 1.0F, 0.0F},
    });
    const cubey::TransformNodeId grandchild = hierarchy.create_node(cubey::Transform3D{
        .translation = {0.0F, 0.0F, 2.0F},
    });
    hierarchy.set_parent(child, root);
    hierarchy.set_parent(grandchild, child);

    hierarchy.update_world_matrices();

    const cubey::math::Vec4 world =
        hierarchy.world_affine_matrix(grandchild) * cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    require_close(world.x, 1.0F, "3D grandchild should inherit root x");
    require_close(world.y, 3.0F, "3D grandchild should inherit child y");
    require_close(world.z, 5.0F, "3D grandchild should inherit local z");
    require_close(world.w, 1.0F, "3D hierarchy should preserve homogeneous w");
}

void test_transform_hierarchy_reparenting_preserves_local_transform() {
    cubey::TransformHierarchy3D hierarchy;
    const cubey::TransformNodeId root_a = hierarchy.create_node(cubey::Transform3D{
        .translation = {10.0F, 0.0F, 0.0F},
    });
    const cubey::TransformNodeId root_b = hierarchy.create_node(cubey::Transform3D{
        .translation = {20.0F, 0.0F, 0.0F},
    });
    const cubey::TransformNodeId child = hierarchy.create_node(cubey::Transform3D{
        .translation = {1.0F, 0.0F, 0.0F},
    });

    hierarchy.set_parent(child, root_a);
    const cubey::math::Vec4 under_a =
        hierarchy.world_affine_matrix(child) * cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    require_close(under_a.x, 11.0F, "Child should inherit first parent");

    hierarchy.set_parent(child, root_b);
    const std::optional<cubey::TransformNodeId> parent = hierarchy.parent(child);
    require_true(parent.has_value() && parent->index == root_b.index,
                 "Child should report its new parent");
    require_close(hierarchy.local_transform(child).translation.x, 1.0F,
                  "Reparenting should preserve local transform");

    const cubey::math::Vec4 under_b =
        hierarchy.world_affine_matrix(child) * cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    require_close(under_b.x, 21.0F, "Child should inherit second parent");

    hierarchy.clear_parent(child);
    require_true(!hierarchy.parent(child).has_value(), "Child should report no parent after clear");
    const cubey::math::Vec4 unparented =
        hierarchy.world_affine_matrix(child) * cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    require_close(unparented.x, 1.0F, "Clearing parent should keep local transform");
}

void test_transform_hierarchy_rejects_invalid_parenting() {
    cubey::TransformHierarchy2D hierarchy;
    const cubey::TransformNodeId root = hierarchy.create_node(cubey::Transform2D{});
    const cubey::TransformNodeId child = hierarchy.create_node(cubey::Transform2D{});
    const cubey::TransformNodeId invalid{99U};

    require_throws([&hierarchy, root]() { hierarchy.set_parent(root, root); },
                   "Hierarchy should reject self-parenting");
    require_throws([&hierarchy, root, invalid]() { hierarchy.set_parent(root, invalid); },
                   "Hierarchy should reject invalid parent IDs");
    require_throws([&hierarchy, invalid]() { (void)hierarchy.local_transform(invalid); },
                   "Hierarchy should reject invalid local transform reads");

    hierarchy.set_parent(child, root);
    require_throws([&hierarchy, root, child]() { hierarchy.set_parent(root, child); },
                   "Hierarchy should reject parenting cycles");
}
