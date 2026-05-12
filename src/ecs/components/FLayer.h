#pragma once

namespace game {

/// Z-ordering component: lower values render first (background), higher = foreground.
struct FLayer {
    int depth{0};
};

} // namespace game
