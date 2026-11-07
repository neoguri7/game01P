#include "gameplay/combat/FStatusRules.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/Logger.h"

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

int EffectiveStatusDuration(const gameplay::FStatusContent& status, int durationDelta) {
    return std::max(1, status.durationTurns + durationDelta);
}

int EffectiveStatusMagnitude(const gameplay::FStatusContent& status, int magnitudeDelta) {
    return std::max(0, status.magnitude + magnitudeDelta);
}

void ApplyStatus(
    FActiveStatuses& active,
    const gameplay::FStatusContent& status,
    int durationDelta,
    int magnitudeDelta) {
    const int duration = EffectiveStatusDuration(status, durationDelta);
    const int magnitude = EffectiveStatusMagnitude(status, magnitudeDelta);
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

bool IsOnApplyControl(const gameplay::FStatusContent& status) {
    return status.kind == gameplay::kStatusKindControl && status.mode == gameplay::kStatusModeOnApply;
}

int ControlApplyChance(const gameplay::FStatusContent& status, int magnitudeDelta) {
    // max(0, cap): a hand-built row may carry a negative cap; std::clamp's precondition is lo <= hi.
    return std::clamp(status.magnitude + magnitudeDelta, 0, std::max(0, status.chanceCap));
}

int ControlTickChance(int effectiveMagnitude, int chanceCap) {
    // 빙결류: magnitude 0 = 항상(100) — 구 TickStatuses 규칙을 그대로 보존한다. 확률형은 저장 강도를
    // 작가 상한으로 자른다.
    return effectiveMagnitude <= 0 ? 100 : std::min(effectiveMagnitude, std::max(0, chanceCap));
}

bool TryApplyStatus(
    FActiveStatuses& active,
    const gameplay::FStatusContent& status,
    int durationDelta,
    int magnitudeDelta,
    FBattleRng& rng) {
    if (!IsOnApplyControl(status)) {
        ApplyStatus(active, status, durationDelta, magnitudeDelta); // 빙결/dot/stat/turn_start: 굴림 없음
        return true;
    }
    const int chance = ControlApplyChance(status, magnitudeDelta);
    if (!rng.rollChance(chance)) {
        LOG_INFO("status '{}' resisted (chance {}%).", status.id, chance);
        return false; // 실패 = 미부여. 0%는 여기서 굴림도 소비하지 않는다(rollChance 규약).
    }
    ApplyStatus(active, status, durationDelta, magnitudeDelta);
    for (FStatusEffect& effect : active.effects) {
        if (effect.statusId == status.id) {
            effect.skipPending = true; // 미소비 상실 1회 — 이미 true면 그대로(중복 없음, D-12)
            break;
        }
    }
    return true;
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
                if (IsOnApplyControl(*definition)) {
                    // D-12: 판정은 부여 시 1회. 틱은 skipPending을 소비만 한다(추가 굴림 없음).
                    if (effect.skipPending) {
                        result.skipTurn = true;
                        effect.skipPending = false;
                    }
                } else if (rng.rollChance(ControlTickChance(effect.magnitude, definition->chanceCap))) {
                    result.skipTurn = true;
                }
            }
        }
        --effect.turnsRemaining;
    }
    std::erase_if(active.effects, [](const FStatusEffect& effect) { return effect.turnsRemaining <= 0; });
    return result;
}

bool IsTurnSkipped(
    const FActiveStatuses& active, const std::vector<const gameplay::FStatusContent*>& definitions) {
    for (const FStatusEffect& effect : active.effects) {
        const gameplay::FStatusContent* definition = FindDefinition(definitions, effect.statusId);
        if (definition == nullptr || definition->kind != gameplay::kStatusKindControl) {
            continue;
        }
        if (IsOnApplyControl(*definition)) {
            if (effect.skipPending) {
                return true; // 부여 시 확정된 미소비 상실
            }
        } else if (ControlTickChance(effect.magnitude, definition->chanceCap) == 100) {
            return true; // 빙결: 턴 시작에 반드시 상실(확률 100 = 확정)
        }
        // 확률 < 100(레거시 turn_start 마비)은 아직 미확정 — 의도를 무효화하지 않는다(TickStatuses가 굴린다).
    }
    return false;
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

int StatusSpeedValue(
    int baseSpeed,
    const FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions) {
    double factor = 1.0;
    for (const FStatusEffect& effect : active.effects) {
        const gameplay::FStatusContent* definition = FindDefinition(definitions, effect.statusId);
        if (definition != nullptr && definition->speedPct != gameplay::kStatusSpeedPctNeutral) {
            factor *= static_cast<double>(definition->speedPct) / gameplay::kStatusSpeedPctNeutral;
        }
    }
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(baseSpeed) * factor)));
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
