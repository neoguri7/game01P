#include "gameplay/combat/FIntent.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiBrain.h"
#include "gameplay/ai/FAiLibrary.h"
#include "gameplay/ai/FAiQueries.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/ai/FAiWorld.h"
#include "gameplay/ai/FSkillRole.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace game {

EIntentMode IntentModeOf(std::string_view target, std::string_view shape) {
    if (target == "cell" || shape == "spread" || shape == "zone" || shape == "pierce") return EIntentMode::Tile;
    return EIntentMode::Unit;
}

namespace {

/// WP-42: one whiff, counted under its reason so the reason counts always sum to `whiffed`.
void CountWhiff(FIntentStats& stats, std::string_view reason) {
    ++stats.whiffed;
    ++stats.whiffReasons[std::string(reason)];
}

/// The mode rule (WP-11 §모드 결정) applied to a *resolved* skill. Resolved values are used on purpose — a
/// rune can change the shape, and the intent must follow the skill the unit really carries.
[[nodiscard]] EIntentMode ModeForSkill(const FResolvedSkill& skill) {
    return IntentModeOf(skill.target, skill.shape);
}

[[nodiscard]] bool IsAttackSkill(const FResolvedSkill& skill) {
    return ai::ClassifySkill(skill) == ai::ESkillRole::Damage;
}

[[nodiscard]] const ai::FAiProfile* ProfileFor(const entt::registry& registry, entt::entity enemy) {
    const auto* library = registry.ctx().find<ai::FAiLibrary>();
    if (library == nullptr) {
        return nullptr;
    }
    if (const auto* brain = registry.try_get<ai::FAiBrain>(enemy); brain != nullptr) {
        if (const ai::FAiProfile* profile = library->find(brain->profileId); profile != nullptr) {
            return profile;
        }
    }
    return &library->fallback();
}

[[nodiscard]] std::optional<FResolvedSkill> ResolveIntentSkill(
    entt::registry& registry, entt::entity enemy, const std::string& skillId,
    const gameplay::FContentRegistry& content) {
    const auto* loadout = registry.try_get<FSkillLoadout>(enemy);
    if (loadout == nullptr) {
        return std::nullopt;
    }
    for (const FSkillSlot& slot : loadout->slots) {
        if (slot.skillId == skillId) {
            return ResolveSkillSlot(slot, content);
        }
    }
    return std::nullopt;
}

/// True when the unit holds an active move_lock status (WP-16): the tick at the owner's turn start zeroes
/// its move pool, so from every other frame the unit must read as unable to move. The lock never touches
/// the skill pool.
[[nodiscard]] bool HoldsMoveLock(entt::registry& registry, entt::entity unit) {
    const auto* active = registry.try_get<FActiveStatuses>(unit);
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (active == nullptr || content == nullptr) {
        return false;
    }
    for (const gameplay::FStatusContent* definition : CollectStatuses(*active, *content)) {
        if (definition != nullptr && definition->kind == gameplay::kStatusKindMoveLock) {
            return true;
        }
    }
    return false;
}

/// Is a move command available to `unit` on its own turn? Planning and evaluation also run OUTSIDE the
/// unit's turn (round-start commits, the per-frame UI query), when the AP pools still hold last turn's
/// leftover: the pools refill at the unit's own turn start (TurnFlowSystem). The unit's own turn reads
/// the live pool; any other frame reads moveMax — what the pool will hold when the turn really comes —
/// except under an active move_lock (WP-16), whose tick zeroes the move pool at the owner's turn start:
/// that unit reads as unable to move from every frame, and planning must not assume a move it cannot make.
[[nodiscard]] bool MoveAvailableOnOwnTurn(entt::registry& registry, entt::entity unit) {
    const auto* points = registry.try_get<FActionPoints>(unit);
    if (points == nullptr) {
        return false;
    }
    // The current-actor mirror is written by the turn flow every update (FCombatRuntime), so it is fresh
    // even on the frame a round begins while the presentation is busy — a direct snapshot read of the
    // turn controller is not available to this gameplay module (layer boundary: gameplay must not reach
    // into the ecs systems).
    if (const auto* combat = registry.ctx().find<FCombatRuntime>(); combat != nullptr && combat->currentActor == unit) {
        return points->move > 0; // the turn-start tick already zeroed a locked unit's pool
    }
    if (HoldsMoveLock(registry, unit)) {
        return false;
    }
    return points->moveMax > 0;
}

/// HP lost to the spikes along the route a move command would walk to `destination`. Mirrors the AI's
/// TrapDamageOnRoute rule (FAiQueries) without needing a built FAiWorld: TrapDamageOnRoute is only
/// callable with an FBtContext, and the intent evaluator must stay a pure registry query.
[[nodiscard]] int PathTrapCost(
    entt::registry& registry, entt::entity enemy, const FGridPosition& from, const FGridPosition& destination,
    int moveRange) {
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    if (terrain == nullptr || destination == from) {
        return 0;
    }
    int traps = 0;
    for (const FGridPosition& step : FindGridPath(registry, enemy, destination, moveRange)) {
        if (terrain->at(step.x, step.y) == ETile::Trap) {
            ++traps;
        }
    }
    return traps * terrain->trapDamage;
}

/// A move that makes progress toward `targetCell`, or nullopt when no reachable cell gets closer. The
/// whiff ends the turn either way — this is only the enemy's last useless step toward its mark.
[[nodiscard]] std::optional<FGridPosition> ApproachToward(
    entt::registry& registry, entt::entity enemy, const FGridPosition& from, const FGridPosition& targetCell) {
    if (!MoveAvailableOnOwnTurn(registry, enemy)) {
        return std::nullopt;
    }
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    const int moveRange = terrain != nullptr ? terrain->moveRange : 1;
    std::optional<FGridPosition> best;
    for (const FGridPosition& cell : ComputeReachableCells(registry, enemy, moveRange)) {
        if (!best || GridDistance(cell, targetCell) < GridDistance(*best, targetCell) ||
            (GridDistance(cell, targetCell) == GridDistance(*best, targetCell) &&
             (cell.y < best->y || (cell.y == best->y && cell.x < best->x)))) {
            best = cell;
        }
    }
    if (!best || GridDistance(*best, targetCell) >= GridDistance(from, targetCell)) {
        return std::nullopt; // no step gets closer: a move would be pure forfeit
    }
    return best;
}

/// Where one move command reaches when only stone blocks the way and bodies are ignored — the movement
/// span that tells a body-block ("blocked") from plain distance ("out of reach"). Walls and the grid edge
/// still bind; occupancy does not. The public grid query cannot express this (it always excludes occupied
/// cells), and the verdict must stay a pure registry query.
[[nodiscard]] std::vector<FGridPosition> GhostReachableCells(
    entt::registry& registry, entt::entity enemy, int maxDistance) {
    std::vector<FGridPosition> cells;
    const auto* start = registry.try_get<FGridPosition>(enemy);
    if (start == nullptr || maxDistance <= 0 || start->x < 0 || start->y < 0 || start->x >= kGridWidth ||
        start->y >= kGridHeight) {
        return cells;
    }
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    std::array<int, kGridWidth * kGridHeight> distance;
    distance.fill(-1);
    std::queue<FGridPosition> pending;
    pending.push(*start);
    distance[static_cast<std::size_t>(start->y * kGridWidth + start->x)] = 0;
    while (!pending.empty()) {
        const FGridPosition current = pending.front();
        pending.pop();
        const int index = current.y * kGridWidth + current.x;
        if (distance[static_cast<std::size_t>(index)] >= maxDistance) {
            continue;
        }
        for (const FGridPosition next : OrthogonalNeighbors(current)) {
            if (next.x < 0 || next.y < 0 || next.x >= kGridWidth || next.y >= kGridHeight ||
                (terrain != nullptr && terrain->at(next.x, next.y) == ETile::Wall)) {
                continue;
            }
            const int nextIndex = next.y * kGridWidth + next.x;
            if (distance[static_cast<std::size_t>(nextIndex)] != -1) {
                continue;
            }
            distance[static_cast<std::size_t>(nextIndex)] = distance[static_cast<std::size_t>(index)] + 1;
            pending.push(next);
            cells.push_back(next);
        }
    }
    return cells;
}

/// The verdict of the unit-mode reach check plus the cell to fire from. "Candidate standing cells" are
/// where one move command can end (walls and bodies included through ComputeReachableCells) plus the
/// current cell; a cell qualifies when it is in range with line of sight to the target.
struct FUnitReach {
    std::optional<FGridPosition> cell;
    EIntentVerdict verdict{EIntentVerdict::Valid};
    std::string reason;
};

[[nodiscard]] FUnitReach FindUnitFiringCell(
    entt::registry& registry, entt::entity enemy, const FGridPosition& from, const FGridPosition& targetCell,
    int range) {
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    const int moveRange = terrain != nullptr ? terrain->moveRange : 1;
    // The span follows the move availability this frame (own turn: live AP; elsewhere: moveMax). A unit
    // that cannot move has no route for bodies to block, so its ghost span is 0 — its failure is distance.
    const bool canMove = MoveAvailableOnOwnTurn(registry, enemy);

    std::vector<FGridPosition> candidates;
    candidates.push_back(from);
    if (canMove) {
        for (const FGridPosition& cell : ComputeReachableCells(registry, enemy, moveRange)) {
            candidates.push_back(cell);
        }
    }

    // Best qualifying cell: trap-free route first, then fewest steps, then (y, x) — the existing AI's
    // trap cost, Gloomhaven's fewest-moves focus, and a deterministic cell order.
    std::optional<FGridPosition> best;
    int bestTrap = 0;
    int bestPath = 0;
    for (const FGridPosition& cell : candidates) {
        if (!IsInRange(cell, targetCell, range) || !HasGridLineOfSight(registry, cell, targetCell)) {
            continue;
        }
        const int path = cell == from ? 0 : static_cast<int>(FindGridPath(registry, enemy, cell, moveRange).size());
        const int trap = PathTrapCost(registry, enemy, from, cell, moveRange);
        const bool better = !best || trap < bestTrap || (trap == bestTrap && path < bestPath) ||
            (trap == bestTrap && path == bestPath &&
             (cell.y < best->y || (cell.y == best->y && cell.x < best->x)));
        if (better) {
            best = cell;
            bestTrap = trap;
            bestPath = path;
        }
    }
    if (best) {
        return FUnitReach{best, EIntentVerdict::Valid, ""};
    }

    // Why the unit cannot reach its mark this turn (WP-11 §실행 규칙, exact strings):
    //  * "blocked": within the caster's movement span a firing cell exists, but bodies sit on the route —
    //    an ally body-block reads "blocked", never "out of reach" (the span ignores bodies on purpose);
    //  * "no line of sight": range holds for a standing cell but stone blocks every sight line;
    //  * "out of reach": nothing in range at all.
    std::vector<FGridPosition> ghost = GhostReachableCells(registry, enemy, canMove ? moveRange : 0);
    for (const FGridPosition& cell : ghost) {
        if (std::ranges::find(candidates, cell) != candidates.end()) {
            continue; // already offered as a real candidate; its failure was not occupancy
        }
        if (IsInRange(cell, targetCell, range) && HasGridLineOfSight(registry, cell, targetCell)) {
            return FUnitReach{std::nullopt, EIntentVerdict::Whiff, "blocked"};
        }
    }
    for (const FGridPosition& cell : candidates) {
        if (IsInRange(cell, targetCell, range)) {
            return FUnitReach{std::nullopt, EIntentVerdict::Whiff, "no line of sight"};
        }
    }
    for (const FGridPosition& cell : ghost) {
        if (IsInRange(cell, targetCell, range)) {
            return FUnitReach{std::nullopt, EIntentVerdict::Whiff, "no line of sight"};
        }
    }
    return FUnitReach{std::nullopt, EIntentVerdict::Whiff, "out of reach"};
}

/// A living unit of the side `caster` fights, standing on `cell` — what a unit-targeted tile shot can be
/// aimed through in the command API (the resolver aims unit-targeted skills at the target's own cell).
[[nodiscard]] entt::entity HostileOccupantOnCell(entt::registry& registry, entt::entity caster, const FGridPosition& cell) {
    const entt::entity occupant = FindGridOccupant(registry, cell.x, cell.y);
    if (occupant == entt::null) {
        return entt::null;
    }
    const bool casterIsParty = registry.all_of<FPartyMember>(caster);
    if (registry.all_of<FPartyMember>(occupant) == casterIsParty) {
        return entt::null; // an ally on the tile is not a legal target for a hostile skill
    }
    return occupant;
}

/// Expected damage of one cast, in the preview's own terms: Σ min(damage, current HP) over the hostile
/// targets — the exact number the reliability invariant compares an execution against.
[[nodiscard]] int PreviewDamage(
    entt::registry& registry, entt::entity caster, const FResolvedSkill& skill, const FGridPosition& aim,
    const FGridPosition& origin, std::vector<std::pair<entt::entity, int>>* outTargets) {
    const auto preview = PreviewSkill(registry, caster, skill, aim, origin);
    if (!preview) {
        return 0;
    }
    const bool casterIsParty = registry.all_of<FPartyMember>(caster);
    int total = 0;
    for (const FPreviewTarget& target : preview->targets) {
        if (registry.all_of<FPartyMember>(target.unit) == casterIsParty) {
            continue; // the caster's own side is not what an intent shot threatens
        }
        const auto* health = registry.try_get<FHealth>(target.unit);
        const int hp = health != nullptr ? std::max(0, health->current) : 0;
        total += std::min(target.damage, hp);
        if (outTargets != nullptr) {
            outTargets->emplace_back(target.unit, hp);
        }
    }
    return total;
}

[[nodiscard]] std::string EntityTag(entt::entity entity) {
    return "#" + std::to_string(static_cast<std::uint64_t>(entt::to_integral(entity)));
}

[[nodiscard]] const char* ModeName(EIntentMode mode) {
    return mode == EIntentMode::Tile ? "tile" : "unit";
}

/// WP §계측 dump head: `<name>#<id> -> <skill> @ (x,y) dmg <n> mode=<tile|unit>` — move intents drop the
/// damage/mode tail (`-> move @ (x,y)`). Decided events append `[<kind>:<reason>]` (invalid/whiff), so a
/// grep for one intent shows the same head on every line of its life.
[[nodiscard]] std::string IntentLine(const std::string& name, entt::entity enemy, const FIntent& intent) {
    std::string line = name + EntityTag(enemy) + " -> ";
    if (intent.skillId.empty()) {
        line += "move @ (" + std::to_string(intent.aim.x) + "," + std::to_string(intent.aim.y) + ")";
        return line;
    }
    line += intent.skillId + " @ (" + std::to_string(intent.aim.x) + "," + std::to_string(intent.aim.y) +
        ") dmg " + std::to_string(intent.expectedAtCommit) + " mode=" + ModeName(intent.mode);
    return line;
}

[[nodiscard]] std::string IntentLine(
    const std::string& name, entt::entity enemy, const FIntent& intent, std::string_view kind,
    const std::string& reason) {
    return IntentLine(name, enemy, intent) + " [" + std::string(kind) + ":" + reason + "]";
}

} // namespace

std::optional<FIntentPlan> PlanIntent(
    entt::registry& registry, entt::entity enemy, const gameplay::FContentRegistry& content) {
    if (!IsAlive(registry, enemy)) {
        return std::nullopt;
    }
    const bool enemyIsParty = registry.all_of<FPartyMember>(enemy);
    if (AliveCount(registry, !enemyIsParty) == 0) {
        return std::nullopt; // nobody hostile left to declare anything at
    }
    const auto* enemyCell = registry.try_get<FGridPosition>(enemy);
    if (enemyCell == nullptr) {
        return std::nullopt;
    }
    const ai::FAiProfile* profile = ProfileFor(registry, enemy);
    if (profile == nullptr) {
        return std::nullopt; // no library: no tuning to judge options with (a wiring bug is reported elsewhere)
    }

    // A LOCAL scratch blackboard/memory: planning must never touch the actor's real brain (the tree and
    // its memory belong to its own decisions; RunBrain with a foreign token would poison both).
    ai::FAiWorld world = ai::BuildAiWorld(registry, enemy, content);
    if (world.self() == nullptr) {
        return std::nullopt;
    }
    ai::FAiBlackboard blackboard;
    ai::FBtMemory memory;
    ai::FBtContext ctx{registry, content, profile->tuning, world, blackboard, memory, enemy, FTurnToken{}, !enemyIsParty};
    const std::optional<EIntentMode> forced = profile->intentMode;

    // Partition the loadout by mode. Unit-mode skills are evaluated from every cell one move command
    // reaches (plus the standing cell) — the execution turn will have a fresh move AP via the AP refill
    // (MoveAvailableOnOwnTurn makes the plan assume exactly that). Tile-mode skills only from the standing
    // cell: a move-then-tile plan would be a whiff by its own execution rule.
    std::vector<std::string> unitSkillIds;
    std::vector<std::string> tileSkillIds;
    for (const std::optional<FResolvedSkill>& skill : world.selfSkills) {
        if (!skill) {
            continue;
        }
        const EIntentMode mode = forced.has_value() ? *forced : ModeForSkill(*skill);
        if (mode == EIntentMode::Tile) {
            tileSkillIds.push_back(skill->skillId);
        } else {
            unitSkillIds.push_back(skill->skillId);
        }
    }

    ai::FSkillFilter unitFilter;
    unitFilter.roles = ai::kRolesHostile;
    unitFilter.skillIds = unitSkillIds;
    ai::FSkillFilter tileFilter;
    tileFilter.roles = ai::kRolesHostile;
    tileFilter.skillIds = tileSkillIds;

    struct FCandidate {
        FGridPosition cell{};
        ai::FSkillOption option;
        EIntentMode mode{EIntentMode::Unit};
        int moves{0};
    };
    std::vector<FCandidate> candidates;

    const auto consider = [&](const FGridPosition& cell, const ai::FSkillFilter& filter, EIntentMode mode) {
        if (filter.skillIds.empty()) {
            return;
        }
        const std::optional<ai::FSkillOption> option = ai::FindBestOption(ctx, cell, filter, false);
        if (!option) {
            return;
        }
        const int moves = cell == *enemyCell
            ? 0
            : static_cast<int>(FindGridPath(registry, enemy, cell, world.moveRange).size());
        candidates.push_back(FCandidate{cell, *option, mode, moves});
    };

    std::vector<FGridPosition> standing{*enemyCell};
    // The move span follows the refill rule: at commit time this enemy is usually NOT the current actor,
    // so its live pool is last turn's leftover — the plan must assume the refill its own turn start does.
    if (MoveAvailableOnOwnTurn(registry, enemy)) {
        for (const FGridPosition& cell : ComputeReachableCells(registry, enemy, world.moveRange)) {
            standing.push_back(cell);
        }
    }
    for (const FGridPosition& cell : standing) {
        consider(cell, unitFilter, EIntentMode::Unit);
    }
    consider(*enemyCell, tileFilter, EIntentMode::Tile);

    if (!candidates.empty()) {
        // Rank like FindBestOption (value per AP, then value, then loadout order); ties break by fewest
        // moves, then the cell (y, x), then the target entity id — the commit order stays deterministic.
        const auto better = [&](const FCandidate& candidate, const FCandidate& best) {
            const int candidateRate = candidate.option.value * 100 / std::max(1, candidate.option.apCost);
            const int bestRate = best.option.value * 100 / std::max(1, best.option.apCost);
            if (candidateRate != bestRate) return candidateRate > bestRate;
            if (candidate.option.value != best.option.value) return candidate.option.value > best.option.value;
            if (candidate.option.slot != best.option.slot) return candidate.option.slot < best.option.slot;
            if (candidate.moves != best.moves) return candidate.moves < best.moves;
            if (candidate.cell.y != best.cell.y) return candidate.cell.y < best.cell.y;
            if (candidate.cell.x != best.cell.x) return candidate.cell.x < best.cell.x;
            return entt::to_integral(candidate.option.target) < entt::to_integral(best.option.target);
        };
        const FCandidate* best = &candidates.front();
        for (const FCandidate& candidate : candidates) {
            if (better(candidate, *best)) {
                best = &candidate;
            }
        }

        const FResolvedSkill& skill = *world.selfSkills[static_cast<std::size_t>(best->option.slot)];
        FIntentPlan plan;
        plan.mode = best->mode;
        plan.skillId = skill.skillId;
        plan.attack = IsAttackSkill(skill);
        plan.aim = best->option.cell;
        plan.target = best->option.target;
        if (plan.mode == EIntentMode::Unit) {
            if (plan.target == entt::null) {
                // A unit intent needs a unit: fall back to whoever stands on the option cell; with nobody
                // there the plan can only be a tile after all.
                plan.target = HostileOccupantOnCell(registry, enemy, best->option.cell);
                if (plan.target == entt::null) {
                    plan.mode = EIntentMode::Tile;
                }
            }
            if (plan.target != entt::null) {
                if (const auto* targetCell = registry.try_get<FGridPosition>(plan.target); targetCell != nullptr) {
                    plan.aim = *targetCell; // the unit's position at commit; execution tracks it live
                }
            }
        }
        plan.expectedAtCommit = PreviewDamage(registry, enemy, skill, plan.aim, best->cell, nullptr);
        return plan;
    }

    // Nothing to fire with this turn: commit an approach instead (the next round's tile/unit candidate).
    std::optional<FGridPosition> destination;
    for (const FGridPosition& cell : standing) {
        if (!destination || ai::NearestHostileDistance(ctx, cell) < ai::NearestHostileDistance(ctx, *destination) ||
            (ai::NearestHostileDistance(ctx, cell) == ai::NearestHostileDistance(ctx, *destination) &&
             (ai::DangerAt(ctx, cell) < ai::DangerAt(ctx, *destination) ||
              (ai::DangerAt(ctx, cell) == ai::DangerAt(ctx, *destination) &&
               (cell.y < destination->y || (cell.y == destination->y && cell.x < destination->x)))))) {
            destination = cell;
        }
    }
    if (!destination || *destination == *enemyCell) {
        return std::nullopt; // already as close as one move allows
    }
    FIntentPlan plan;
    plan.mode = EIntentMode::Unit;
    plan.target = entt::null;
    plan.aim = *destination;
    plan.expectedAtCommit = 0;
    plan.attack = false;
    return plan;
}

FIntentEvaluation EvaluateIntent(
    entt::registry& registry, entt::entity enemy, const gameplay::FContentRegistry& content) {
    FIntentEvaluation evaluation;
    auto* store = registry.ctx().find<FIntentStore>();
    if (store == nullptr) {
        return evaluation;
    }
    const auto it = store->intents.find(enemy);
    if (it == store->intents.end()) {
        return evaluation; // nothing committed: nothing to invalidate
    }
    const FIntent& intent = it->second;

    if (!IsAlive(registry, enemy)) {
        evaluation.verdict = EIntentVerdict::Invalidated;
        evaluation.reason = "incapacitated";
        return evaluation;
    }
    if (const auto* active = registry.try_get<FActiveStatuses>(enemy);
        active != nullptr && !active->effects.empty() &&
        IsTurnSkipped(*active, CollectStatuses(*active, content))) {
        evaluation.verdict = EIntentVerdict::Invalidated;
        evaluation.reason = "incapacitated";
        return evaluation;
    }

    if (intent.skillId.empty()) {
        // Move intent: usable while one move can still land on the planned cell or make progress toward
        // it. Fully boxed in (wall or bodies) is the one invalidation.
        const auto* cell = registry.try_get<FGridPosition>(enemy);
        if (cell == nullptr) {
            evaluation.verdict = EIntentVerdict::Invalidated;
            evaluation.reason = "blocked cell";
            return evaluation;
        }
        const auto* terrain = registry.ctx().find<FGridTerrain>();
        const int moveRange = terrain != nullptr ? terrain->moveRange : 1;
        // The reach reflects the move the unit will have on its own turn (live AP there, moveMax here).
        const std::vector<FGridPosition> reachable = MoveAvailableOnOwnTurn(registry, enemy)
            ? ComputeReachableCells(registry, enemy, moveRange)
            : std::vector<FGridPosition>{};
        if (std::ranges::find(reachable, intent.aim) != reachable.end()) {
            return evaluation;
        }
        for (const FGridPosition& candidate : reachable) {
            if (GridDistance(candidate, intent.aim) < GridDistance(*cell, intent.aim)) {
                return evaluation; // re-pathing makes progress; the exact destination is replaceable
            }
        }
        evaluation.verdict = EIntentVerdict::Invalidated;
        evaluation.reason = "blocked cell";
        return evaluation;
    }

    const std::optional<FResolvedSkill> resolved = ResolveIntentSkill(registry, enemy, intent.skillId, content);
    const auto* cell = registry.try_get<FGridPosition>(enemy);
    if (!resolved || cell == nullptr) {
        evaluation.verdict = EIntentVerdict::Invalidated;
        evaluation.reason = "incapacitated"; // a validated loadout cannot lose its skill: wiring bug fallback
        return evaluation;
    }

    if (intent.mode == EIntentMode::Tile) {
        // The committed cell must still be a legal target; a caster that can no longer reach it with range
        // and line of sight is "displaced" (it can never re-aim — that is the dodge's whole point).
        const auto* terrain = registry.ctx().find<FGridTerrain>();
        if (terrain != nullptr && terrain->at(intent.aim.x, intent.aim.y) == ETile::Wall) {
            evaluation.verdict = EIntentVerdict::Invalidated;
            evaluation.reason = "blocked cell";
            return evaluation;
        }
        if (!IsInRange(*cell, intent.aim, resolved->range) ||
            !HasGridLineOfSight(registry, *cell, intent.aim)) {
            evaluation.verdict = EIntentVerdict::Whiff;
            evaluation.reason = "displaced";
            return evaluation;
        }
        if (resolved->target != "cell" && HostileOccupantOnCell(registry, enemy, intent.aim) == entt::null) {
            // A unit-targeted area skill is fired through the occupant of its committed tile: the command
            // API aims unit-targeted skills at the target's live cell (the resolver owns that contract), so
            // an empty tile has no one to aim through. Limit — kept deliberately: the footprint around the
            // empty center is NOT fired either (a dodger one cell outside the center is safe). Lifting this
            // needs the resolver to accept a cell aim for unit-targeted area skills (out of this WP).
            evaluation.verdict = EIntentVerdict::Whiff;
            evaluation.reason = "tile empty";
        }
        return evaluation;
    }

    if (intent.target == entt::null || !IsAlive(registry, intent.target)) {
        evaluation.verdict = EIntentVerdict::Invalidated;
        evaluation.reason = "target dead";
        return evaluation;
    }
    const auto* targetCell = registry.try_get<FGridPosition>(intent.target);
    if (targetCell == nullptr) {
        evaluation.verdict = EIntentVerdict::Invalidated;
        evaluation.reason = "target dead";
        return evaluation;
    }

    const FUnitReach reach = FindUnitFiringCell(registry, enemy, *cell, *targetCell, resolved->range);
    if (!reach.cell) {
        evaluation.verdict = EIntentVerdict::Whiff;
        evaluation.reason = reach.reason;
    }
    return evaluation;
}

FIntentStep TakeCommittedIntent(
    entt::registry& registry, const FTurnToken& token, const gameplay::FContentRegistry& content) {
    FIntentStep step;
    auto* store = registry.ctx().find<FIntentStore>();
    if (store == nullptr) {
        return step;
    }
    const auto it = store->intents.find(token.actor);
    if (it == store->intents.end()) {
        return step;
    }
    FIntent& intent = it->second;
    const std::string name = UnitDisplayName(registry, token.actor);

    if (intent.state == EIntentState::WrapUp) {
        step.kind = EIntentStepKind::EndTurn; // its whiff was already decided: the dodge costs the turn
        return step;
    }

    const FIntentEvaluation evaluation = EvaluateIntent(registry, token.actor, content);
    if (evaluation.verdict == EIntentVerdict::Invalidated) {
        if (intent.attack) {
            // The rates count attack intents only (lead decision 6): a move or debuff intent invalidating
            // is logged, never counted — whiffed/committed above 100% would be a fake metric.
            ++store->stats.invalidated;
        }
        LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "invalid", evaluation.reason));
        store->intents.erase(it);
        return step; // Fallback: the tree recomputes what is left of this turn (갱신)
    }

    const auto* cell = registry.try_get<FGridPosition>(token.actor);
    if (cell == nullptr) {
        if (intent.attack) {
            ++store->stats.invalidated;
        }
        LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "invalid", "blocked cell"));
        store->intents.erase(it);
        return step;
    }

    if (intent.skillId.empty()) {
        // Move intent: one move toward the planned destination; when it is occupied or gone, take the best
        // progress a single move still makes toward it, then hand the rest of the turn back to the tree.
        if (!MoveAvailableOnOwnTurn(registry, token.actor)) {
            if (intent.attack) {
                ++store->stats.invalidated;
            }
            LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "invalid", "blocked cell"));
            store->intents.erase(it);
            return step;
        }
        const auto* terrain = registry.ctx().find<FGridTerrain>();
        const int moveRange = terrain != nullptr ? terrain->moveRange : 1;
        const std::vector<FGridPosition> reachable = ComputeReachableCells(registry, token.actor, moveRange);
        std::optional<FGridPosition> destination;
        if (std::ranges::find(reachable, intent.aim) != reachable.end()) {
            destination = intent.aim;
        } else {
            for (const FGridPosition& candidate : reachable) {
                if (!destination ||
                    GridDistance(candidate, intent.aim) < GridDistance(*destination, intent.aim) ||
                    (GridDistance(candidate, intent.aim) == GridDistance(*destination, intent.aim) &&
                     (candidate.y < destination->y ||
                      (candidate.y == destination->y && candidate.x < destination->x)))) {
                    destination = candidate;
                }
            }
        }
        if (!destination || *destination == *cell) {
            if (intent.attack) {
                ++store->stats.invalidated;
            }
            LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "invalid", "blocked cell"));
            store->intents.erase(it);
            return step;
        }
        step.kind = EIntentStepKind::Command;
        step.action = FMoveRequest{token.actor, *destination, token};
        return step;
    }

    const std::optional<FResolvedSkill> resolved = ResolveIntentSkill(registry, token.actor, intent.skillId, content);
    if (!resolved) {
        if (intent.attack) {
            ++store->stats.invalidated;
        }
        LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "invalid", "incapacitated"));
        store->intents.erase(it);
        return step;
    }

    if (intent.mode == EIntentMode::Tile) {
        if (evaluation.verdict == EIntentVerdict::Whiff) {
            // Already displaced or aimed at an empty tile (EvaluateIntent owns the verdict — WP-12 shows
            // the same reason): the shot does not happen and the turn ends, no move.
            if (intent.attack) {
                CountWhiff(store->stats, evaluation.reason);
            }
            intent.state = EIntentState::WrapUp;
            LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "whiff", evaluation.reason));
            step.kind = EIntentStepKind::EndTurn;
            return step;
        }
        if (resolved->target == "cell") {
            step.kind = EIntentStepKind::Command;
            step.action = FActionRequest{token.actor, entt::null, intent.skillId, token, intent.aim};
            return step;
        }
        // A unit-targeted area skill fires through whoever stands on the committed tile (the resolver
        // aims unit-targeted skills at the target's live cell, so the tile is held by its occupant).
        // EvaluateIntent reports the empty-tile case as a "tile empty" whiff, so this branch is the
        // defensive twin of that verdict.
        const entt::entity occupant = HostileOccupantOnCell(registry, token.actor, intent.aim);
        if (occupant == entt::null) {
            if (intent.attack) {
                CountWhiff(store->stats, "tile empty");
            }
            intent.state = EIntentState::WrapUp;
            LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "whiff", "tile empty"));
            step.kind = EIntentStepKind::EndTurn;
            return step;
        }
        step.kind = EIntentStepKind::Command;
        step.action = FActionRequest{token.actor, occupant, intent.skillId, token, intent.aim};
        return step;
    }

    if (evaluation.verdict == EIntentVerdict::Whiff) {
        // Approach as far as this turn allows, then end the turn: the dodge must cost the enemy its turn,
        // otherwise the tree would simply re-target and dodging would be free (W5 review #6).
        if (const auto* targetCell = registry.try_get<FGridPosition>(intent.target); targetCell != nullptr) {
            if (const std::optional<FGridPosition> approach =
                    ApproachToward(registry, token.actor, *cell, *targetCell)) {
                step.kind = EIntentStepKind::Command;
                step.whiffWrapUp = true;
                step.reason = evaluation.reason; // Acknowledge logs it once the submit really landed
                step.action = FMoveRequest{token.actor, *approach, token};
                return step;
            }
        }
        if (intent.attack) {
            CountWhiff(store->stats, evaluation.reason);
        }
        intent.state = EIntentState::WrapUp;
        LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "whiff", evaluation.reason));
        step.kind = EIntentStepKind::EndTurn;
        return step;
    }

    // Valid: fire from the current cell, or position first and fire on the next step.
    const auto* targetCell = registry.try_get<FGridPosition>(intent.target);
    const FUnitReach reach = FindUnitFiringCell(registry, token.actor, *cell, *targetCell, resolved->range);
    if (reach.cell.has_value() && *reach.cell == *cell) {
        step.kind = EIntentStepKind::Command;
        step.action = FActionRequest{token.actor, intent.target, intent.skillId, token, *targetCell};
        return step;
    }
    if (reach.cell.has_value()) {
        step.kind = EIntentStepKind::Command;
        step.action = FMoveRequest{token.actor, *reach.cell, token};
        return step;
    }
    // Unreachable in the same frame the evaluation passed: defensive only (no world change happened).
    if (intent.attack) {
        CountWhiff(store->stats, reach.reason);
    }
    intent.state = EIntentState::WrapUp;
    LOG_INFO("[intent] {}", IntentLine(name, token.actor, intent, "whiff", reach.reason));
    step.kind = EIntentStepKind::EndTurn;
    return step;
}

void AcknowledgeIntentStep(
    entt::registry& registry, entt::entity enemy, const FIntentStep& step,
    const gameplay::FContentRegistry& content) {
    if (step.kind != EIntentStepKind::Command || !step.action.has_value()) {
        return;
    }
    auto* store = registry.ctx().find<FIntentStore>();
    if (store == nullptr) {
        return;
    }
    const auto it = store->intents.find(enemy);
    if (it == store->intents.end()) {
        return;
    }
    FIntent& intent = it->second;
    const std::string name = UnitDisplayName(registry, enemy);
    const std::string tag = EntityTag(enemy);

    if (std::holds_alternative<FMoveRequest>(*step.action)) {
        if (step.whiffWrapUp) {
            // The closing approach of a whiff: the whiff is now real (counted once here, not in Take —
            // Take must stay side-effect free until the submit proved it was accepted). The reason comes
            // from the take step so the line keeps the same head as its commit and whiff siblings.
            if (intent.attack) {
                CountWhiff(store->stats, step.reason.empty() ? "closing approach" : step.reason);
            }
            intent.state = EIntentState::WrapUp;
            LOG_INFO("[intent] {}", IntentLine(name, enemy, intent, "whiff",
                                               step.reason.empty() ? "closing approach" : step.reason));
            return;
        }
        if (intent.skillId.empty()) {
            store->intents.erase(it); // the move intent executed; the tree owns the rest of the turn
        }
        return; // a positioning move for a skill intent: the next take re-evaluates from the new cell
    }

    const auto* request = std::get_if<FActionRequest>(&*step.action);
    if (request == nullptr) {
        return;
    }
    if (intent.skillId.empty()) {
        store->intents.erase(it); // defensive: a move intent never carries an action request
        return;
    }

    // The shot left: record what the preview promised and whose HP it promised it against. The settle
    // helper measures it once the action queue has drained.
    store->execution = FIntentExecution{};
    FIntentExecution& execution = store->execution;
    execution.sessionId = store->sessionId;
    execution.enemy = enemy;
    execution.skillId = intent.skillId;
    execution.aim = request->cell;
    execution.expectedAtCommit = intent.expectedAtCommit;
    execution.attack = intent.attack;
    if (const std::optional<FResolvedSkill> resolved = ResolveIntentSkill(registry, enemy, intent.skillId, content);
        resolved.has_value()) {
        if (const auto* enemyCell = registry.try_get<FGridPosition>(enemy); enemyCell != nullptr) {
            execution.expectedAtExecution = PreviewDamage(registry, enemy, *resolved, request->cell, *enemyCell,
                                                          &execution.targets);
        }
    }
    store->hasExecution = true;
    LOG_INFO("[intent] {}{} fires {} @ ({},{}) expected {} dmg", name, tag, execution.skillId, execution.aim.x,
             execution.aim.y, execution.expectedAtExecution);
    store->intents.erase(it);
}

void SettleIntentExecution(entt::registry& registry, std::uint64_t sessionId) {
    auto* store = registry.ctx().find<FIntentStore>();
    if (store == nullptr || !store->hasExecution) {
        return;
    }
    const auto* queue = registry.ctx().find<FActionQueue>();
    if (queue == nullptr) {
        // No battle owns the queue: the shot can never land (it died with its battle). Drop it.
        store->hasExecution = false;
        store->execution = FIntentExecution{};
        return;
    }
    if (!queue->pending.empty()) {
        return; // the command is still in flight
    }
    const FIntentExecution execution = store->execution;
    store->hasExecution = false;
    store->execution = FIntentExecution{};
    if (sessionId == 0 || sessionId != execution.sessionId) {
        return; // a previous battle's shot must not be measured against this one's registry
    }

    int actual = 0;
    for (const auto& [unit, hpBefore] : execution.targets) {
        int hpNow = 0;
        if (const auto* health = registry.try_get<FHealth>(unit)) {
            hpNow = health->current;
        }
        actual += std::max(0, hpBefore - hpNow);
    }
    const bool hit = actual > 0;
    store->stats.lastExecuted = FIntentStats::FLastExecuted{
        execution.skillId, execution.aim, execution.expectedAtCommit, execution.expectedAtExecution, actual, hit};
    if (execution.attack && !hit) {
        // It fired, it hit nobody: the tile dodge, or a shot whose target slipped away. Same bucket as the
        // reach whiffs — the whiff rate is about attacks that failed to land.
        CountWhiff(store->stats, "hit nobody");
        LOG_INFO("[intent] {}{} whiffed (0 of {} expected dmg)", UnitDisplayName(registry, execution.enemy),
                 EntityTag(execution.enemy), execution.expectedAtExecution);
    } else {
        LOG_INFO("[intent] {}{} hit {} for {} (expected {})", UnitDisplayName(registry, execution.enemy),
                 EntityTag(execution.enemy), execution.skillId, actual, execution.expectedAtExecution);
    }
}

void CommitEnemyIntents(entt::registry& registry, const gameplay::FContentRegistry& content) {
    auto* bus = registry.ctx().find<FEventBus>();
    if (bus == nullptr) {
        return;
    }
    const std::vector<FRoundStarted>& rounds = bus->frameEvents<FRoundStarted>();
    if (rounds.empty()) {
        return;
    }
    const FTurnSnapshot& snapshot = rounds.back().snapshot;

    auto* store = registry.ctx().find<FIntentStore>();
    if (store == nullptr) {
        store = &registry.ctx().emplace<FIntentStore>();
    }
    if (store->sessionId != snapshot.sessionId) {
        // A new battle owns the store now (the same guard shape as FCommandResults): the previous
        // battle's intents and telemetry must not leak into it.
        *store = FIntentStore{};
        store->sessionId = snapshot.sessionId;
    }
    if (store->committedRound == snapshot.roundNumber) {
        return; // the same (sessionId, round) commits exactly once
    }

    // Leftovers: intents whose declarer never got to execute them (skipped, dead, the battle ran out of
    // turns). Settled exactly once; WrapUp entries were already counted when they whiffed. Like every
    // settlement, only an attack intent feeds `invalidated` — the log keeps every event (lead decision 6).
    for (const auto& [enemy, intent] : store->intents) {
        if (intent.state != EIntentState::Declared) {
            continue;
        }
        if (intent.attack) {
            ++store->stats.invalidated;
        }
        LOG_INFO("[intent] {}", IntentLine(UnitDisplayName(registry, enemy), enemy, intent, "invalid", "leftover"));
    }
    store->intents.clear();
    store->committedRound = snapshot.roundNumber;

    // Commit in entity-id ascending order: deterministic regardless of the speed-tie shuffle.
    std::vector<entt::entity> enemies;
    for (const entt::entity enemy : registry.view<FEnemy, FGridPosition>()) {
        if (IsAlive(registry, enemy)) {
            enemies.push_back(enemy);
        }
    }
    std::ranges::sort(enemies, [](entt::entity left, entt::entity right) {
        return entt::to_integral(left) < entt::to_integral(right);
    });

    for (const entt::entity enemy : enemies) {
        const std::optional<FIntentPlan> plan = PlanIntent(registry, enemy, content);
        if (!plan) {
            continue;
        }
        FIntent intent;
        intent.mode = plan->mode;
        intent.skillId = plan->skillId;
        intent.target = plan->target;
        intent.aim = plan->aim;
        intent.expectedAtCommit = plan->expectedAtCommit;
        intent.committedRound = snapshot.roundNumber;
        intent.attack = plan->attack;
        if (plan->skillId.empty()) {
            ++store->stats.moveIntents;
        } else if (plan->attack) {
            // committed == tileCommitted + unitCommitted counts ATTACK intents only (lead decision 6).
            ++store->stats.committed;
            if (plan->mode == EIntentMode::Tile) {
                ++store->stats.tileCommitted;
            } else {
                ++store->stats.unitCommitted;
            }
        }
        LOG_INFO("[intent] {}", IntentLine(UnitDisplayName(registry, enemy), enemy, intent));
        store->intents.emplace(enemy, std::move(intent));
    }
}

const FIntentStats* FindIntentStats(const entt::registry& registry) {
    const auto* store = registry.ctx().find<FIntentStore>();
    return store != nullptr ? &store->stats : nullptr;
}

} // namespace game
