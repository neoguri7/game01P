#pragma once

namespace game::ecs {
    // negative control: pure-data POD component, no virtual, no flags
    struct FZzGood { float radius{0.5f}; int layer{0}; };
}
