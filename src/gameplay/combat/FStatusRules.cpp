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

void ApplyStatus(
    FActiveStatuses& active,
    const gameplay::FStatusContent& status,
    int durationDelta,
    int magnitudeDelta) {
    const int duration = std::max(1, status.durationTurns + durationDelta);
    const int magnitude = std::max(0, status.magnitude + magnitudeDelta);
    for (FStatusEffect& effect : active.effects) {
        if (effect.statusId == status.id) {
            // 갱신(§4): 지속은 긴 쪽을 유지하고, 강도는 최신 적용값을 쓴다.
            effect.turnsRemaining = std::max(effect.turnsRemaining, duration);
            effect.magnitude = magnitude;
            return;
        }
    }
    active.effects.push_back(FStatusEffect{status.id, duration, magnitude});
}

FStatusTickResult TickStatuses(
    FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions,
    FBattleRng& rng) {
    FStatusTickResult result;
    for (FStatusEffect& effect : active.effects) {
        const gameplay::FStatusContent* definition = FindDefinition(definitions, effect.statusId);
        if (definition != nullptr) {
            if (definition->kind == gameplay::kStatusKindDot) {
                result.damage += std::max(0, effect.magnitude);
            } else if (definition->kind == gameplay::kStatusKindControl) {
                // 마비는 확률(%), 빙결은 magnitude 0 = 항상 행동 불가. 감전 룬이 확률을 올린다(§3.3).
                const int chance = effect.magnitude > 0 ? effect.magnitude : 100;
                if (rng.rollChance(chance)) {
                    result.skipTurn = true;
                }
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
            total += effect.magnitude;
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
