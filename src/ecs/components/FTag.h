#pragma once
#include <string>

namespace game {

/// Lightweight tag for ECS entity lookup (e.g. "player", "enemy", "ui_layer").
/// Stored as std::string for safe ownership.
struct FTag {
    std::string value;

    bool operator==(std::string_view sv) const { return value == sv; }
};

} // namespace game
