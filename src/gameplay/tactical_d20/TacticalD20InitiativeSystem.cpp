#include "gameplay/tactical_d20/TacticalD20InitiativeSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FAbilityScores.h"
#include "ecs/components/FCombatStateInitiativeRolling.h"
#include "ecs/components/FCombatStateRoundStart.h"
#include "ecs/components/FInitiativeRoll.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FTacticalTurnOrder.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20Random.h"
#include "gameplay/tactical_d20/FTacticalD20Rules.h"
#include "gameplay/tactical_d20/FTacticalD20StateLog.h"

#include <algorithm>
#include <fmt/format.h>
#include <random>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

constexpr int D20Sides = 20;

void EnsureInitiativeServices(entt::registry& registry) {
    if (!registry.ctx().contains<FTacticalD20Random>()) registry.ctx().emplace<FTacticalD20Random>();
    if (!registry.ctx().contains<FTacticalD20StateLog>()) registry.ctx().emplace<FTacticalD20StateLog>();
}

void AppendStateLog(entt::registry& registry, const std::string& line) {
    if (auto* log = registry.ctx().find<FTacticalD20StateLog>()) {
        log->lines.push_back(line);
        if (auto* config = registry.ctx().find<FTacticalD20Config>()) {
            while (static_cast<int>(log->lines.size()) > config->logging.stateLogMaxLines) {
                log->lines.erase(log->lines.begin());
            }
        }
    }
}

void PublishStateChange(entt::registry& registry, const char* previousState, const char* nextState) {
    AppendStateLog(registry, fmt::format("[CombatState] {} -> {}", previousState, nextState));
    const FTacticalD20CombatStateChangedEvent event{previousState, nextState};
    PUBLISH(FTacticalD20CombatStateChangedEvent, registry, event);
}

bool IsPlayerTeam(const FTacticalUnit& unit) {
    return unit.team == "player";
}

std::vector<entt::entity> LivingUnits(entt::registry& registry) {
    std::vector<entt::entity> units;
    auto view = registry.view<FTacticalUnit, FAbilityScores>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) units.push_back(entity);
    return units;
}

int RollD20(entt::registry& registry) {
    auto& random = registry.ctx().get<FTacticalD20Random>();
    std::uniform_int_distribution<int> distribution(1, D20Sides);
    return distribution(random.rng);
}

void RollInitiative(entt::registry& registry, entt::entity entity) {
    const auto& abilities = registry.get<FAbilityScores>(entity);
    const int dexterityModifier = AbilityModifier(abilities, ETacticalD20Ability::Dexterity);
    const int naturalRoll = RollD20(registry);
    registry.emplace_or_replace<FInitiativeRoll>(entity, naturalRoll, dexterityModifier, naturalRoll + dexterityModifier);
}

void SortTurnOrder(entt::registry& registry, std::vector<entt::entity>& units) {
    std::ranges::sort(units, [&registry](entt::entity left, entt::entity right) {
        const auto& leftRoll = registry.get<FInitiativeRoll>(left);
        const auto& rightRoll = registry.get<FInitiativeRoll>(right);
        if (leftRoll.total != rightRoll.total) return leftRoll.total > rightRoll.total;
        if (leftRoll.dexterityModifier != rightRoll.dexterityModifier) return leftRoll.dexterityModifier > rightRoll.dexterityModifier;
        return IsPlayerTeam(registry.get<FTacticalUnit>(left)) && !IsPlayerTeam(registry.get<FTacticalUnit>(right));
    });
}

void StoreTurnOrder(entt::registry& registry, entt::entity stateEntity, std::vector<entt::entity> units) {
    if (registry.all_of<FTacticalTurnOrder>(stateEntity)) {
        auto& order = registry.get<FTacticalTurnOrder>(stateEntity);
        order.units = std::move(units);
        order.currentIndex = -1;
        order.round = 0;
        return;
    }
    registry.emplace<FTacticalTurnOrder>(stateEntity, std::move(units), -1, 0);
}

void TransitionInitiativeToRoundStart(entt::registry& registry, entt::entity stateEntity) {
    // Transition table:
    //   FCombatStateInitiativeRolling + all living units rolled initiative -> FCombatStateRoundStart
    registry.remove<FCombatStateInitiativeRolling>(stateEntity);
    registry.emplace<FCombatStateRoundStart>(stateEntity);
    PublishStateChange(registry, "InitiativeRolling", "RoundStart");
}

} // namespace

void TacticalD20InitiativeSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20InitiativeSystem");
    auto stateView = registry.view<FCombatStateInitiativeRolling>();
    if (stateView.empty()) return;

    EnsureInitiativeServices(registry);
    for (auto stateEntity : stateView) {
        auto units = LivingUnits(registry);
        for (auto unitEntity : units) RollInitiative(registry, unitEntity);
        SortTurnOrder(registry, units);
        StoreTurnOrder(registry, stateEntity, std::move(units));
        TransitionInitiativeToRoundStart(registry, stateEntity);
    }
}

} // namespace game
