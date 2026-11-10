#include "ecs/systems/EmberCombatInfo.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/systems/EmberCombatHudSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "presentation/FUiFrame.h"

namespace game::ecs {
namespace {

using gameplay::FContentRegistry;
using gameplay::FStatusContent;

constexpr const char* kGlyphBoots = "boots";
constexpr const char* kGlyphSword = "sword";
constexpr const char* kGlyphBow = "bow";
constexpr const char* kGlyphSpell = "spell";

/// Mirrors FIntent.cpp's file-local MoveAvailableOnOwnTurn (WP-11:104-120): the unit's own turn reads the
/// live move pool, any other frame reads moveMax (the refill assumption), and an active move_lock reads as
/// unable to move from every frame. FIntent.cpp is not this WP's file, so the rule is mirrored here in one
/// named helper — follow-up: expose it from FIntent.h and delete this copy.
[[nodiscard]] bool CanMoveOnOwnTurn(entt::registry& registry, entt::entity unit) {
    const auto* points = registry.try_get<FActionPoints>(unit);
    if (points == nullptr) {
        return false;
    }
    if (const auto* combat = registry.ctx().find<FCombatRuntime>(); combat != nullptr && combat->currentActor == unit) {
        return points->move > 0;
    }
    const auto* active = registry.try_get<FActiveStatuses>(unit);
    const auto* content = registry.ctx().find<FContentRegistry>();
    if (active != nullptr && content != nullptr) {
        for (const FStatusContent* definition : CollectStatuses(*active, *content)) {
            if (definition != nullptr && definition->kind == gameplay::kStatusKindMoveLock) {
                return false;
            }
        }
    }
    return points->moveMax > 0;
}

/// Same lookup as FIntent.cpp's ResolveIntentSkill (the intent's loadout slot -> ResolveSkillSlot).
[[nodiscard]] std::optional<FResolvedSkill> ResolveCommittedSkill(
    entt::registry& registry, entt::entity enemy, const std::string& skillId, const FContentRegistry& content) {
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

/// The existing icon vocabulary (EDungeonSprite::IconSword/Bow/Spell/Boots) as a render-free token.
[[nodiscard]] std::string GlyphFor(const FResolvedSkill* skill) {
    if (skill == nullptr) {
        return kGlyphSpell;
    }
    if (skill->damageType == gameplay::kDamageTypeMagical) {
        return kGlyphSpell;
    }
    return skill->range > 2 ? kGlyphBow : kGlyphSword;
}

[[nodiscard]] std::string SkillName(const FContentRegistry& content, const std::string& skillId) {
    const auto* row = content.skills.find(skillId);
    return row != nullptr ? row->displayName : skillId;
}

/// Expected damage of one intent, in the preview's own terms: Σ min(damage, current HP) over the party
/// targets — the semantics of FIntent.cpp's file-local PreviewDamage (:298-320), recomputed here every
/// frame because buffs/debuffs legitimately change between commit and execution (StS Weak precedent).
struct FIntentNumbers {
    int damage{0};
    bool lethal{false};
    std::vector<std::pair<entt::entity, int>> perTarget;
};

[[nodiscard]] FIntentNumbers PreviewIntentNumbers(
    entt::registry& registry, entt::entity enemy, const FResolvedSkill& skill, const FGridPosition& aim,
    const FGridPosition& origin) {
    FIntentNumbers numbers;
    const std::optional<FSkillPreview> preview = PreviewSkill(registry, enemy, skill, aim, origin);
    if (!preview) {
        return numbers;
    }
    const bool casterIsParty = registry.all_of<FPartyMember>(enemy);
    for (const FPreviewTarget& target : preview->targets) {
        if (registry.all_of<FPartyMember>(target.unit) == casterIsParty) {
            continue;  // the caster's own side is not what an intent shot threatens
        }
        const auto* health = registry.try_get<FHealth>(target.unit);
        const int hp = health != nullptr ? std::max(0, health->current) : 0;
        const int applied = std::min(target.damage, hp);
        numbers.damage += applied;
        numbers.lethal = numbers.lethal || target.lethal;
        numbers.perTarget.emplace_back(target.unit, applied);
    }
    return numbers;
}

/// Threat-ranked order: a valid lethal shot outranks a plain attack, which outranks support/move; an
/// invalid intent is drawn last (dimmed) and never enters the threat totals.
[[nodiscard]] int PriorityOf(const FIntentBadge& badge) {
    if (!badge.valid) {
        return 3;
    }
    if (badge.lethal) {
        return 0;
    }
    return badge.attack ? 1 : 2;
}

/// Every cell an enemy could strike after one move: its reachable cells plus its standing cell, then every
/// in-grid cell within the committed skill's range with line of sight from one of those standings — the
/// same primitives the WP-11 execution rule uses (ComputeReachableCells, range, HasGridLineOfSight).
[[nodiscard]] std::vector<FGridPosition> ComputeReachCells(
    entt::registry& registry, entt::entity enemy, const FResolvedSkill& skill) {
    std::vector<FGridPosition> strikeCells;
    const auto* enemyCell = registry.try_get<FGridPosition>(enemy);
    if (enemyCell == nullptr) {
        return strikeCells;
    }
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    const int moveRange = terrain != nullptr ? terrain->moveRange : 1;
    std::vector<FGridPosition> standing{*enemyCell};
    if (CanMoveOnOwnTurn(registry, enemy)) {
        for (const FGridPosition& cell : ComputeReachableCells(registry, enemy, moveRange)) {
            standing.push_back(cell);
        }
    }
    for (int y = 0; y < kGridHeight; ++y) {
        for (int x = 0; x < kGridWidth; ++x) {
            if (terrain != nullptr && terrain->at(x, y) == ETile::Wall) {
                continue;
            }
            const FGridPosition cell{x, y};
            for (const FGridPosition& origin : standing) {
                if (IsInRange(origin, cell, skill.range) && HasGridLineOfSight(registry, origin, cell)) {
                    strikeCells.push_back(cell);
                    break;
                }
            }
        }
    }
    return strikeCells;
}

/// The cells of one intent that the board paints as threatened (lead decision 3):
///  * tile mode + a cell-target skill (firebomb): the resolved footprint around the committed cell;
///  * tile mode + a unit-targeted area skill (cleave): ONLY the centre cell — the resolver aims such a
///    skill through the tile's occupant (WP-11 deviation 5), so the footprint is not reachable by design;
///  * unit mode: the tracked target's current cell.
[[nodiscard]] std::vector<FGridPosition> ComputeDangerCells(
    const FIntent& intent, const FResolvedSkill& skill, const FGridPosition& aim, const FGridPosition& enemyCell) {
    std::vector<FGridPosition> cells;
    if (intent.skillId.empty()) {
        return cells;  // a move intent threatens nothing
    }
    if (intent.mode == EIntentMode::Tile) {
        if (skill.target == "cell") {
            return ComputeSkillCells(skill, intent.aim, enemyCell);
        }
        cells.push_back(intent.aim);
        return cells;
    }
    cells.push_back(aim);
    return cells;
}

/// Fills the snapshot's move-hover path (only when the hovered cell is one the HUD offered as reachable).
/// The reachable list is the HUD's own (`FGridSelection::reachable`): it is empty when the acting unit has
/// no move AP left, so the board never draws a path the unit cannot walk. Reads the existing path helper;
/// a trap on the walked path is reported separately for the board overlay.
void BuildMovePreview(entt::registry& registry, FCombatInfoSnapshot& snapshot, entt::entity actor,
                      const FGridPosition& hovered) {
    const auto* selection = registry.ctx().find<FGridSelection>();
    if (selection == nullptr || std::ranges::find(selection->reachable, hovered) == selection->reachable.end()) {
        return;
    }
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    const int moveRange = terrain != nullptr ? terrain->moveRange : 1;
    snapshot.movePath = FindGridPath(registry, actor, hovered, moveRange);
    snapshot.moveDestination = hovered;
    if (terrain == nullptr) {
        return;
    }
    for (const FGridPosition& step : snapshot.movePath) {
        if (terrain->at(step.x, step.y) == ETile::Trap) {
            snapshot.trapCells.push_back(step);
        }
    }
}

/// The selected skill over this frame's hovered target/cell, in WP-03's numbers (no re-derivation).
void BuildSkillPreview(entt::registry& registry, FCombatInfoSnapshot& snapshot) {
    const auto* combat = registry.ctx().find<FCombatRuntime>();
    const auto* selection = registry.ctx().find<FGridSelection>();
    const auto* runtime = registry.ctx().find<FTurnRuntime>();
    const auto* content = registry.ctx().find<FContentRegistry>();
    if (combat == nullptr || selection == nullptr || !selection->hovered || runtime == nullptr || content == nullptr) {
        return;
    }
    const FTurnSnapshot turn = runtime->controller.snapshot();
    if (!turn.currentTurn) {
        return;
    }
    const entt::entity actor = turn.currentTurn->actor;
    if (!IsAlive(registry, actor) || !registry.all_of<FPartyMember, FSkillLoadout, FGridPosition>(actor)) {
        return;
    }
    if (const auto* queue = registry.ctx().find<FActionQueue>(); queue != nullptr && !queue->pending.empty()) {
        return;
    }
    const auto& slots = registry.get<FSkillLoadout>(actor).slots;
    const int selected = combat->selectedSkill;
    if (selected < 0 || selected >= static_cast<int>(slots.size())) {
        return;
    }
    const std::optional<FResolvedSkill> skill = ResolveSkillSlot(slots[selected], *content);
    if (!skill) {
        return;
    }
    const FGridPosition actorCell = registry.get<FGridPosition>(actor);
    const FGridPosition hovered = *selection->hovered;

    FGridPosition aim{};
    bool usable = false;
    if (skill->target == "cell") {
        aim = hovered;
        usable = IsInRange(actorCell, hovered, skill->range) && HasGridLineOfSight(registry, actorCell, hovered);
    } else if (const entt::entity target = FindGridOccupant(registry, hovered.x, hovered.y);
               target != entt::null && CanTargetWithSkill(registry, actor, target, *skill)) {
        aim = registry.get<FGridPosition>(target);
        usable = true;
    }
    if (!usable) {
        BuildMovePreview(registry, snapshot, actor, hovered);
        return;
    }

    const std::optional<FSkillPreview> preview = PreviewSkill(registry, actor, *skill, aim, actorCell);
    if (!preview) {
        return;
    }
    for (const FPreviewTarget& target : preview->targets) {
        const auto* cell = registry.try_get<FGridPosition>(target.unit);
        if (cell == nullptr) {
            continue;
        }
        snapshot.preview.push_back(FPreviewBadge{*cell, target.damage, target.heal, target.lethal, target.critDamage,
                                                 preview->critChancePercent});
    }
}

}  // namespace

FCombatInfoSnapshot BuildCombatInfoSnapshot(entt::registry& registry) {
    FCombatInfoSnapshot snapshot;

    if (const auto* runtime = registry.ctx().find<FTurnRuntime>()) {
        const FTurnSnapshot turn = runtime->controller.snapshot();
        for (const FTurnEntry& entry : turn.entries) {
            if (entry.state == ETurnEntryState::Skipped || entry.state == ETurnEntryState::Completed) {
                continue;
            }
            snapshot.order.currentRound.push_back(UnitDisplayName(registry, entry.entity));
        }
    }

    const auto* content = registry.ctx().find<FContentRegistry>();
    if (content == nullptr) {
        return snapshot;
    }

    const int compactThreshold = [&registry] {
        if (const ui::FUiLayout* layout = ui::FindUiLayout(registry); layout != nullptr) {
            return layout->intentCompactThreshold;
        }
        return ui::kDefaultIntentCompactThreshold;
    }();

    struct FBadgeWithNumbers {
        FIntentBadge badge;
        FIntentNumbers numbers;
        FIntent intent;
    };
    std::vector<FBadgeWithNumbers> badges;

    if (auto* store = registry.ctx().find<FIntentStore>(); store != nullptr) {
        for (const auto& [enemy, intent] : store->intents) {
            FIntentBadge badge;
            badge.enemy = enemy;
            badge.attack = intent.attack;

            const std::optional<FResolvedSkill> skill = intent.skillId.empty()
                ? std::nullopt
                : ResolveCommittedSkill(registry, enemy, intent.skillId, *content);
            badge.glyph = intent.skillId.empty() ? kGlyphBoots : GlyphFor(skill ? &*skill : nullptr);

            // Aim: unit mode tracks the committed unit's CURRENT cell; tile mode keeps the committed cell.
            FGridPosition aim = intent.aim;
            if (intent.mode == EIntentMode::Unit && intent.target != entt::null) {
                if (const auto* targetCell = registry.try_get<FGridPosition>(intent.target)) {
                    aim = *targetCell;
                }
            }
            badge.cell = aim;

            const FIntentEvaluation evaluation = EvaluateIntent(registry, enemy, *content);
            badge.valid = evaluation.verdict == EIntentVerdict::Valid;

            FBadgeWithNumbers entry;
            entry.intent = intent;
            if (const auto* enemyCell = registry.try_get<FGridPosition>(enemy);
                enemyCell != nullptr && skill.has_value()) {
                badge.dangerCells = ComputeDangerCells(intent, *skill, aim, *enemyCell);
                if (badge.valid && intent.attack) {
                    entry.numbers = PreviewIntentNumbers(registry, enemy, *skill, aim, *enemyCell);
                    badge.expectedDamage = entry.numbers.damage;
                    badge.lethal = entry.numbers.lethal;
                }
            }

            if (!badge.valid) {
                // Reason strings are shown exactly as FIntent produces them (WP-11 contract).
                badge.tooltip = evaluation.reason;
                badge.expectedDamage = 0;
                badge.lethal = false;
                badge.dangerCells.clear();  // an impossible shot must not paint danger cells
            } else if (intent.skillId.empty()) {
                badge.tooltip = "Move to (" + std::to_string(intent.aim.x) + ", " + std::to_string(intent.aim.y) + ")";
            } else {
                badge.tooltip = SkillName(*content, intent.skillId);
                if (intent.attack) {
                    badge.tooltip += " · " + std::to_string(badge.expectedDamage) + " dmg";
                    if (badge.lethal) {
                        badge.tooltip += " · LETHAL";
                    }
                }
            }
            entry.badge = std::move(badge);
            badges.push_back(std::move(entry));
        }
    }

    std::ranges::stable_sort(badges, [](const FBadgeWithNumbers& left, const FBadgeWithNumbers& right) {
        const int leftPriority = PriorityOf(left.badge);
        const int rightPriority = PriorityOf(right.badge);
        if (leftPriority != rightPriority) {
            return leftPriority < rightPriority;
        }
        return entt::to_integral(left.badge.enemy) < entt::to_integral(right.badge.enemy);
    });

    // Threat totals: per living party member, Σ over VALID attack intents of min(damage, HP). The same
    // per-intent preview feeds the badges, so Σ members' incoming always equals Σ valid badges' damage.
    std::map<entt::entity, int> incoming;
    for (const FBadgeWithNumbers& entry : badges) {
        if (!entry.badge.valid || !entry.badge.attack) {
            continue;
        }
        for (const auto& [unit, applied] : entry.numbers.perTarget) {
            incoming[unit] += applied;
        }
    }
    for (const entt::entity unit : registry.view<FPartyMember, FHealth>()) {
        if (!IsAlive(registry, unit)) {
            continue;
        }
        const auto& health = registry.get<FHealth>(unit);
        const int total = incoming.contains(unit) ? incoming.at(unit) : 0;
        snapshot.threats.push_back(FThreatTotal{unit, total, total >= health.current});
    }

    // Hover-only data: the hovered enemy's reachable strike cells for its unit-mode intent.
    const auto* selection = registry.ctx().find<FGridSelection>();
    if (selection != nullptr && selection->hovered) {
        const entt::entity hoveredUnit = FindGridOccupant(registry, selection->hovered->x, selection->hovered->y);
        if (hoveredUnit != entt::null && registry.all_of<FEnemy>(hoveredUnit)) {
            for (FBadgeWithNumbers& entry : badges) {
                if (entry.badge.enemy != hoveredUnit || entry.intent.mode != EIntentMode::Unit ||
                    entry.intent.skillId.empty()) {
                    continue;
                }
                const std::optional<FResolvedSkill> skill =
                    ResolveCommittedSkill(registry, hoveredUnit, entry.intent.skillId, *content);
                if (skill) {
                    entry.badge.reachCells = ComputeReachCells(registry, hoveredUnit, *skill);
                }
            }
        }
    }

    // Collapse same-cell badges behind the highest priority one (its "+N" badge). Damage sums, so the
    // threat invariant keeps holding after the collapse; valid and invalid intents never hide each other.
    snapshot.compact = static_cast<int>(badges.size()) > compactThreshold;
    for (FBadgeWithNumbers& entry : badges) {
        FIntentBadge* kept = nullptr;
        for (FIntentBadge& existing : snapshot.intents) {
            if (existing.cell == entry.badge.cell && existing.valid == entry.badge.valid) {
                kept = &existing;
                break;
            }
        }
        if (kept == nullptr) {
            snapshot.intents.push_back(std::move(entry.badge));
            continue;
        }
        kept->stacked += entry.badge.stacked;
        kept->expectedDamage += entry.badge.expectedDamage;
        kept->lethal = kept->lethal || entry.badge.lethal;
        for (const FGridPosition& cell : entry.badge.dangerCells) {
            if (std::ranges::find(kept->dangerCells, cell) == kept->dangerCells.end()) {
                kept->dangerCells.push_back(cell);
            }
        }
        // A hovered enemy's reach must survive the collapse even when its badge is the hidden one.
        for (const FGridPosition& cell : entry.badge.reachCells) {
            if (std::ranges::find(kept->reachCells, cell) == kept->reachCells.end()) {
                kept->reachCells.push_back(cell);
            }
        }
    }

    BuildSkillPreview(registry, snapshot);
    return snapshot;
}

void StoreCombatInfoSnapshot(entt::registry& registry, FCombatInfoSnapshot snapshot) {
    auto* stored = registry.ctx().find<FCombatInfoSnapshot>();
    if (stored == nullptr) {
        registry.ctx().emplace<FCombatInfoSnapshot>(std::move(snapshot));
        return;
    }
    *stored = std::move(snapshot);
}

const FCombatInfoSnapshot* FindCombatInfoSnapshot(const entt::registry& registry) {
    return registry.ctx().find<FCombatInfoSnapshot>();
}

}  // namespace game::ecs