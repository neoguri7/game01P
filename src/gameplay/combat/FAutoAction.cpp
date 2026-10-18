#include "gameplay/combat/FAutoAction.h"

#include "ecs/components/FActionPoints.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/ai/FAiBrain.h"
#include "gameplay/combat/FCombatQuery.h"

namespace game {

std::optional<FBattleAction> ChooseAutoAction(
    entt::registry& registry,
    FTurnToken token,
    bool targetPartySide,
    const gameplay::FContentRegistry& content) {
    const auto actor = token.actor;
    if (!IsAlive(registry, actor) || !registry.all_of<FSkillLoadout, FGridPosition, FActionPoints>(actor)) {
        return std::nullopt;
    }
    // The decision belongs to the actor's behavior tree (src/gameplay/ai, docs/design/enemy-ai.md). This
    // function stays the one entry point the turn flow knows: it neither schedules turns nor applies actions.
    return ai::RunBrain(registry, token, targetPartySide, content);
}

} // namespace game
