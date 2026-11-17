#include "gameplay/combat/FEliteRules.h"

#include <algorithm>
#include <string>
#include <utility>

#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"

namespace game {
namespace {

constexpr const char* kSpellTag = "Spell";
constexpr const char* kMeleeTag = "Melee";

[[nodiscard]] bool HasTag(const gameplay::FSkillContent& skill, const char* tag) {
    return std::find(skill.tags.begin(), skill.tags.end(), std::string(tag)) != skill.tags.end();
}

[[nodiscard]] FEliteBattleState* MutableState(entt::registry& registry) {
    return registry.ctx().find<FEliteBattleState>();
}

void Feed(entt::registry& registry, std::string line) {
    if (auto* log = registry.ctx().find<FCombatLog>()) log->push(std::move(line));
}

} // namespace

void InstallEliteBattle(entt::registry& registry, const FEliteAffix* affix, int healthPercent) {
    registry.ctx().erase<FEliteBattleState>();
    if (affix == nullptr) return;
    FEliteBattleState state;
    state.affix = *affix;
    state.healthPercent = healthPercent;
    registry.ctx().emplace<FEliteBattleState>(std::move(state));
}

const FEliteBattleState* FindEliteBattle(const entt::registry& registry) {
    return registry.ctx().find<FEliteBattleState>();
}

void ApplyEliteBattleStart(entt::registry& registry) {
    auto* state = MutableState(registry);
    if (state == nullptr) return;
    state->attackBonus = 0;
    state->pendingBonus = 0;
    state->lastRound = 0;
    if (state->affix.rule != EEliteRule::Unyielding) return;
    for (const auto entity : registry.view<FEnemy>()) {
        auto& active = registry.get_or_emplace<FActiveStatuses>(entity);
        for (const auto& id : state->affix.statuses) {
            if (std::find(active.immune.begin(), active.immune.end(), id) == active.immune.end()) {
                active.immune.push_back(id);
            }
        }
    }
}

void EliteOnSkillUsed(entt::registry& registry, entt::entity actor, const gameplay::FSkillContent& skill) {
    auto* state = MutableState(registry);
    if (state == nullptr || state->affix.rule != EEliteRule::SpellFed) return;
    if (!registry.valid(actor) || !registry.all_of<FPartyMember>(actor)) return;
    if (!HasTag(skill, kSpellTag)) return;
    const int room = std::max(0, state->affix.maxBonus - state->attackBonus);
    state->pendingBonus = std::min(state->pendingBonus + state->affix.amount, room);
}

void EliteOnRoundStarted(entt::registry& registry, const gameplay::FContentRegistry& content,
                         std::uint64_t roundNumber) {
    auto* state = MutableState(registry);
    if (state == nullptr) return;
    if (roundNumber == 0 || roundNumber <= state->lastRound) return;
    state->lastRound = roundNumber;
    const int before = state->attackBonus;
    if (state->affix.rule == EEliteRule::SpellFed) {
        state->attackBonus += state->pendingBonus;
        state->pendingBonus = 0;
    } else if (state->affix.rule == EEliteRule::Escalating) {
        if (roundNumber >= static_cast<std::uint64_t>(state->affix.fromRound)) {
            state->attackBonus = std::min(state->attackBonus + state->affix.amount, state->affix.maxBonus);
        }
    }
    const int delta = state->attackBonus - before;
    if (delta == 0) return;
    if (registry.ctx().contains<FDungeonRun>()) {
        for (const auto entity : registry.view<FEnemy>()) RefreshCombatSheet(registry, entity, content);
    }
    Feed(registry, state->affix.displayName + ": enemies +" + std::to_string(delta) + " attack (now +" +
                       std::to_string(state->attackBonus) + ")");
}

void EliteOnHit(entt::registry& registry, const FHookContext& context) {
    const auto* state = FindEliteBattle(registry);
    if (state == nullptr || state->affix.rule != EEliteRule::Thorned) return;
    if (context.amount <= 0) return;
    if (!registry.valid(context.owner) || !registry.valid(context.other)) return;
    if (!registry.all_of<FPartyMember>(context.owner) || !registry.all_of<FEnemy>(context.other)) return;
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (content == nullptr) return;
    const auto* skill = content->skills.find(context.skillId);
    if (skill == nullptr || !HasTag(*skill, kMeleeTag)) return;
    const auto* health = registry.try_get<FHealth>(context.owner);
    if (health == nullptr) return;
    const int loss = std::min(state->affix.amount, health->current - 1);
    if (loss <= 0) return;
    Feed(registry, UnitDisplayName(registry, context.owner) + " takes " + std::to_string(loss) + " damage (Thorned)");
    (void)ApplyDamageToUnit(registry, context.owner, loss);
}

void RegisterEliteRuleListener(entt::registry& registry) {
    RegisterHookListener(registry, gameplay::EHookEvent::OnHit,
                         [](entt::registry& r, const FHookContext& context) { EliteOnHit(r, context); });
}

std::string EliteAffectedSkills(entt::registry& registry, const FEliteAffix& affix) {
    const char* tag = nullptr;
    if (affix.rule == EEliteRule::SpellFed) tag = kSpellTag;
    else if (affix.rule == EEliteRule::Thorned) tag = kMeleeTag;
    if (tag == nullptr) return {};
    std::string names;
    for (const auto* skill : PartySkillRows(registry)) {
        if (skill == nullptr || !HasTag(*skill, tag)) continue;
        if (!names.empty()) names += ", ";
        names += skill->displayName;
    }
    if (names.empty()) return std::string("Your party has no ") + tag + " skills. ";
    return std::string("Your ") + tag + " skills: " + names + ". ";
}

std::string EliteRuleHudLine(entt::registry& registry) {
    const auto* state = FindEliteBattle(registry);
    if (state == nullptr) return {};
    if (const auto* run = registry.ctx().find<FDungeonRun>(); run != nullptr && run->phase != EDungeonPhase::Battle) {
        return {};
    }
    const FEliteAffix& affix = state->affix;
    std::string line = affix.displayName + ": " + affix.text;
    if (affix.rule == EEliteRule::SpellFed || affix.rule == EEliteRule::Thorned) {
        std::string skills = EliteAffectedSkills(registry, affix);
        if (!skills.empty() && skills.back() == ' ') skills.pop_back();
        line += " " + skills;
    }
    if (affix.rule == EEliteRule::SpellFed || affix.rule == EEliteRule::Escalating) {
        line += "  |  enemy attack +" + std::to_string(state->attackBonus);
    }
    if (affix.rule == EEliteRule::SpellFed && state->pendingBonus > 0) {
        line += " (+" + std::to_string(state->pendingBonus) + " next round)";
    }
    return line;
}

} // namespace game
