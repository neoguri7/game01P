#pragma once

#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FGridPosition.h"

namespace game::ecs {

/// WP-12 display contract. Everything the combat HUD and the board overlays show is computed once per
/// frame into this snapshot (render-free, unit-testable); `EmberCombatHudSystem::update` owns the write
/// into the registry context and the view / grid renderer only read it. The builder calls the rules
/// functions the game already runs (WP-03 PreviewSkill, WP-11 EvaluateIntent, FGridQuery) and never
/// re-derives damage or verdict math.

/// The turn ribbon's data. The next round is deliberately NOT an array: the order is decided when the
/// round starts (and can change inside it: speed_pct, deaths, tempo), so only the marker row is shown
/// (W6 review ★12 — a sorted preview would present a guess as a promise).
struct FTurnOrderLine {
    std::vector<std::string> currentRound;
    bool nextRoundDecidedAtRoundStart{true};
};

/// Marker text of the ribbon's second row.
inline constexpr const char* kNextRoundMarker = "Next round: decided at round start (ties drawn)";

/// One enemy's declared intent as the board shows it. `glyph` is an art token ("sword" | "bow" |
/// "spell" | "boots"), mapped to the existing dungeon icons by the view; the snapshot never includes
/// presentation headers.
struct FIntentBadge {
    entt::entity enemy{entt::null};
    FGridPosition cell{};
    std::string glyph{"spell"};
    bool lethal{false};
    bool valid{true};
    bool attack{false};   // number is drawn only for a valid damage-skill intent
    std::string tooltip;
    int expectedDamage{0};  // Σ min(damage, current HP) over the party targets, recomputed every frame
    int stacked{1};         // same-cell intents collapsed behind this one (the "+N" badge)
    std::vector<FGridPosition> dangerCells;  // the cells the declared shot threatens
    std::vector<FGridPosition> reachCells;   // unit-mode hover: where it could stand and still strike
};

/// One target of the selected skill preview (borrowed numbers from WP-03 PreviewSkill).
struct FPreviewBadge {
    FGridPosition cell{};
    int damage{0};
    int heal{0};
    bool lethal{false};
};

/// What one living party member is promised to take this round (same per-frame preview as the badges).
struct FThreatTotal {
    entt::entity unit{entt::null};
    int incomingThisRound{0};
    bool lethalThisRound{false};
};

struct FCombatInfoSnapshot {
    FTurnOrderLine order;
    std::vector<FIntentBadge> intents;  // sorted (lethal > attack > support/invalid), collapsed per cell
    std::vector<FPreviewBadge> preview;
    std::vector<FThreatTotal> threats;
    // Move hover: the walked cells (FindGridPath, start excluded), the destination and the traps on it.
    std::vector<FGridPosition> movePath;
    std::optional<FGridPosition> moveDestination;
    std::vector<FGridPosition> trapCells;
    // Density rule: more intentions than this collapses glyphs/badges to their numbers (ui_layout data).
    bool compact{false};
};

/// Builds this frame's snapshot. Reads FIntentStore (WP-11), FGridSelection's hover, the live turn and
/// the content; no side effects on rules state.
[[nodiscard]] FCombatInfoSnapshot BuildCombatInfoSnapshot(entt::registry& registry);

/// Stores (or clears, with a default snapshot) the frame's snapshot in the registry context.
void StoreCombatInfoSnapshot(entt::registry& registry, FCombatInfoSnapshot snapshot);

/// The snapshot written by this frame's HUD update, or null outside the battle / before the first frame.
[[nodiscard]] const FCombatInfoSnapshot* FindCombatInfoSnapshot(const entt::registry& registry);

} // namespace game::ecs