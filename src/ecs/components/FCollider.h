#pragma once
#include <glm/vec2.hpp>

namespace game {

/// Collision primitive type
enum class EColliderType {
    AABB,
    Circle
};

/// Collision component — attach to any entity that needs physics/collision checks.
struct FCollider {
    EColliderType type = EColliderType::AABB;
    glm::vec2     offset{0.f, 0.f}; ///< Offset from entity center/pivot
    float         halfWidth{16.f};  ///< AABB half-extents (X/Y) or Circle radius
    float         halfHeight{16.f}; ///< AABB only (ignored for Circle)

    /// Layer/group for selective collision (bitmask style, default = 1)
    int collisionLayer{1};

    /// Optional: tag override for collision resolution names
    const char* collisionTag{nullptr};
};

} // namespace game
