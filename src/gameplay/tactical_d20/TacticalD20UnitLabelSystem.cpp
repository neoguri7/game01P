#include "gameplay/tactical_d20/TacticalD20UnitLabelSystem.h"

#include "ecs/components/FConditionBurning.h"
#include "ecs/components/FConditionDodge.h"
#include "ecs/components/FConditionPoisoned.h"
#include "ecs/components/FConditionStunned.h"
#include "ecs/components/FText.h"
#include "ecs/components/FTacticalUnit.h"

#include <fmt/format.h>
#include <string>
#include <tracy/Tracy.hpp>
#include <vector>

namespace game {
namespace {

std::string ConditionsText(entt::registry& registry, entt::entity unit) {
    std::vector<std::string> names;
    if (registry.all_of<FConditionDodge>(unit)) names.push_back("Dodge");
    if (registry.all_of<FConditionPoisoned>(unit)) names.push_back("Poisoned");
    if (registry.all_of<FConditionStunned>(unit)) names.push_back("Stunned");
    if (registry.all_of<FConditionBurning>(unit)) names.push_back("Burning");
    if (names.empty()) return "-";

    std::string text = names.front();
    for (std::size_t i = 1; i < names.size(); ++i) text += ", " + names[i];
    return text;
}

} // namespace

void TacticalD20UnitLabelSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20UnitLabelSystem");
    auto view = registry.view<FTacticalUnit, FText>();
    for (auto entity : view) {
        const auto& unit = view.get<FTacticalUnit>(entity);
        auto& text = view.get<FText>(entity);
        text.content = fmt::format("{} HP {}/{} Conditions: {}", unit.displayName, unit.currentHp, unit.maxHp, ConditionsText(registry, entity));
    }
}

} // namespace game
