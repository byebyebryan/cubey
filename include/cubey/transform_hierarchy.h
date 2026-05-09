#pragma once

#include <cubey/math.h>
#include <cubey/transform_2d.h>
#include <cubey/transform_3d.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace cubey {

struct TransformNodeId {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();

    friend bool operator==(TransformNodeId lhs, TransformNodeId rhs) = default;
};

template <typename TransformT, typename MatrixT> class BasicTransformHierarchy {
  public:
    [[nodiscard]] TransformNodeId create_node(const TransformT& local_transform = TransformT{}) {
        if (nodes_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("transform hierarchy exhausted node ids");
        }

        const TransformNodeId id{static_cast<std::uint32_t>(nodes_.size())};
        nodes_.push_back(Node{
            .local = local_transform,
            .world = local_transform.affine_matrix(),
            .parent = std::nullopt,
            .children = {},
            .dirty = false,
        });
        return id;
    }

    void set_local_transform(TransformNodeId id, const TransformT& local_transform) {
        mutable_node(id).local = local_transform;
        mark_subtree_dirty(id);
    }

    [[nodiscard]] const TransformT& local_transform(TransformNodeId id) const {
        return node(id).local;
    }

    void set_parent(TransformNodeId child, TransformNodeId parent) {
        validate(child);
        validate(parent);
        if (child == parent) {
            throw std::runtime_error("transform hierarchy cannot parent a node to itself");
        }
        if (would_create_cycle(child, parent)) {
            throw std::runtime_error("transform hierarchy parenting would create a cycle");
        }

        Node& child_node = mutable_node(child);
        if (child_node.parent.has_value() && *child_node.parent == parent) {
            return;
        }

        remove_from_parent_children(child, child_node.parent);
        child_node.parent = parent;
        mutable_node(parent).children.push_back(child);
        mark_subtree_dirty(child);
    }

    void clear_parent(TransformNodeId child) {
        Node& child_node = mutable_node(child);
        if (!child_node.parent.has_value()) {
            return;
        }

        remove_from_parent_children(child, child_node.parent);
        child_node.parent = std::nullopt;
        mark_subtree_dirty(child);
    }

    [[nodiscard]] std::optional<TransformNodeId> parent(TransformNodeId id) const {
        return node(id).parent;
    }

    [[nodiscard]] MatrixT world_affine_matrix(TransformNodeId id) {
        validate(id);
        update_world_matrix(id);
        return node(id).world;
    }

    void update_world_matrices() {
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            update_world_matrix(TransformNodeId{static_cast<std::uint32_t>(index)});
        }
    }

  private:
    struct Node {
        TransformT local{};
        MatrixT world{1.0F};
        std::optional<TransformNodeId> parent{};
        std::vector<TransformNodeId> children{};
        bool dirty = true;
    };

    [[nodiscard]] bool contains(TransformNodeId id) const {
        return id.index < nodes_.size();
    }

    void validate(TransformNodeId id) const {
        if (!contains(id)) {
            throw std::runtime_error("transform node id is invalid");
        }
    }

    [[nodiscard]] const Node& node(TransformNodeId id) const {
        validate(id);
        return nodes_[id.index];
    }

    [[nodiscard]] Node& mutable_node(TransformNodeId id) {
        validate(id);
        return nodes_[id.index];
    }

    [[nodiscard]] bool would_create_cycle(TransformNodeId child, TransformNodeId parent) const {
        std::optional<TransformNodeId> current = parent;
        while (current.has_value()) {
            if (*current == child) {
                return true;
            }
            current = node(*current).parent;
        }
        return false;
    }

    void remove_from_parent_children(TransformNodeId child,
                                     std::optional<TransformNodeId> parent_id) {
        if (!parent_id.has_value()) {
            return;
        }

        std::vector<TransformNodeId>& siblings = mutable_node(*parent_id).children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }

    void mark_subtree_dirty(TransformNodeId id) {
        Node& current = mutable_node(id);
        current.dirty = true;
        const std::vector<TransformNodeId> children = current.children;
        for (const TransformNodeId child : children) {
            mark_subtree_dirty(child);
        }
    }

    void update_world_matrix(TransformNodeId id) {
        Node& current = mutable_node(id);
        if (!current.dirty) {
            return;
        }

        const MatrixT local_matrix = current.local.affine_matrix();
        if (current.parent.has_value()) {
            update_world_matrix(*current.parent);
            current.world = node(*current.parent).world * local_matrix;
        } else {
            current.world = local_matrix;
        }
        current.dirty = false;
    }

    std::vector<Node> nodes_{};
};

using TransformHierarchy2D = BasicTransformHierarchy<Transform2D, math::Mat3>;
using TransformHierarchy3D = BasicTransformHierarchy<Transform3D, math::Mat4>;

} // namespace cubey
