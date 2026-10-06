#include "gameplay/combat/FStatusRules.h"

#include <algorithm>
#include <string>

namespace game {
namespace {

[[nodiscard]] const gameplay::FStatusContent* FindDefinition(
    const std::vector<const gameplay::FStatusContent*>& definitions,
    const std::string& statusId) {
    for (const gameplay::FStatusContent* definition : definitions) {
        if (definition != nullptr && definition->id == statusId) {
            return definition;
        }
    }
    return nullptr;
}

} // namespace

void ApplyStatus(FActiveStatuses& active, const gameplay::FStatusContent& status) {
    for (FStatusEffect& effect : active.effects) {
        if (effect.statusId == status.id) {
            effect.turnsRemaining = std::max(effect.turnsRemaining, status.durationTurns);
            return;
        }
    }
    active.effects.push_back(FStatusEffect{status.id, std::max(1, status.durationTurns)});
}

FStatusTickResult TickStatuses(
    FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions) {
    FStatusTickResult result;
    for (FStatusEffect& effect : active.effects) {
        const gameplay::FStatusContent* definition = FindDefinition(definitions, effect.statusId);
        if (definition != nullptr) {
            if (definition->kind == gameplay::kStatusKindDot) {
                result.damage += std::max(0, definition->magnitude);
            } else if (definition->kind == gameplay::kStatusKindControl) {
                result.skipTurn = true;
            }
        }
        --effect.turnsRemaining;
    }
    std::erase_if(active.effects, [](const FStatusEffect& effect) { return effect.turnsRemaining <= 0; });
    return result;
}

int StatusAttackModifier(
    const FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions) {
    int total = 0;
    for (const FStatusEffect& effect : active.effects) {
        const gameplay::FStatusContent* definition = FindDefinition(definitions, effect.statusId);
        if (definition != nullptr && definition->kind == gameplay::kStatusKindStat) {
            total += definition->magnitude;
        }
    }
    return total;
}

std::vector<const gameplay::FStatusContent*> CollectStatuses(
    const FActiveStatuses& active, const gameplay::FContentRegistry& content) {
    std::vector<const gameplay::FStatusContent*> definitions;
    definitions.reserve(active.effects.size());
    for (const FStatusEffect& effect : active.effects) {
        if (const gameplay::FStatusContent* definition = content.statuses.find(effect.statusId)) {
            definitions.push_back(definition);
        }
    }
    return definitions;
}

} // namespace game
