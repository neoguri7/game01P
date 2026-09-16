#pragma once

#include "core/Logger.h"
#include "gameplay/data/FBehaviorContent.h"
#include "gameplay/data/FContentRegistry.h"

#include <cstddef>
#include <string>
#include <vector>

namespace game::gameplay {

/// Everything a behaviour rule is allowed to look at, gathered by the caller.
/// why a plain value struct instead of passing the registry: a condition that needs a new fact has to add it
/// here, and that makes "what can a monster react to?" a single readable list (R12). It also keeps the
/// decision a pure function of state (R19) — no entity iteration order can leak into the choice.
struct FBehaviorContext {
    double selfHealthPct{1.0};
    bool allyDowned{false};
    bool opponentInSkillRange{false};
    std::size_t livingOpponents{0};
    bool actorHasSkill{false};
};

/// The action the behaviour tree picks: the first rule in `behaviorIds` order whose condition holds, or
/// nullptr when no rule matched (the caller then waits — a monster with no applicable rule must not act at
/// random, R19).
///
/// `UseFirstSkill` counts as **not matching** when `context.actorHasSkill` is false, so a skill-less unit
/// falls through to the next rule (usually `move_*` or `wait`) instead of burning its turn on an impossible
/// action. why here and not in the executor: that is a statement about the rule's meaning, not about how the
/// action is carried out (R12/R27).
///
/// `invariant:` `behaviorIds` order is the priority order and is preserved exactly (R16④: the decision may not
/// depend on container iteration order); an id missing from the table is a content bug that FContentRegistry
/// already rejects at load, and is skipped with a warning here so a live battle cannot stall on it.
[[nodiscard]] inline const FBehaviorContent* firstMatchingBehavior(const std::vector<std::string>& behaviorIds,
                                                                  const FContentRegistry& content,
                                                                  const FBehaviorContext& context) {
    for (const std::string& ruleId : behaviorIds) {
        const FBehaviorContent* rule = content.behaviors.find(ruleId);
        if (rule == nullptr) {
            LOG_WARN("behavior: rule '{}' is not in the behaviour table; skipping it.", ruleId);
            continue;
        }

        switch (rule->condition) {
        case EBehaviorCondition::Always:
            break; // 무조건 일치 — 목록의 catch-all
        case EBehaviorCondition::SelfHpAtOrBelow:
            if (context.selfHealthPct > rule->threshold) {
                continue;
            }
            break;
        case EBehaviorCondition::AllyDowned:
            if (!context.allyDowned) {
                continue;
            }
            break;
        case EBehaviorCondition::OpponentInSkillRange:
            if (!context.opponentInSkillRange) {
                continue;
            }
            break;
        case EBehaviorCondition::OpponentCountAtOrAbove:
            // why a cast and not a rounded compare: the threshold is authored as a count, so a fractional
            // value would be a content mistake; truncating keeps the comparison a total order (R19).
            if (context.livingOpponents < static_cast<std::size_t>(rule->threshold)) {
                continue;
            }
            break;
        }

        if (rule->action == EBehaviorAction::UseFirstSkill && !context.actorHasSkill) {
            continue; // 스킬이 없으면 이 규칙은 성립하지 않는다 (위 주석)
        }

        return rule;
    }

    return nullptr;
}

} // namespace game::gameplay
