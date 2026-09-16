#pragma once

#include <string>

// V34 [R2, gameplay coverage]: the same component-purity violation as ecs/components/FZzBad.h, placed in the
// gameplay layer. It exists because slice 2 added src/gameplay/components — a rule whose target list stopped at
// the ecs boundary would keep reporting "clean" while the new layer grew logic-bearing components.
namespace game::gameplay {

struct FZzBadGameplayComponent {
    virtual void tick(float dt);                  // virtual logic inside a component
    virtual ~FZzBadGameplayComponent() = default; // virtual destructor
    std::string* cachedLabel{nullptr};            // component owns a raw pointer
};

} // namespace game::gameplay
