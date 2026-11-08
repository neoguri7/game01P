#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "gameplay/dungeon/FEncounterContent.h"

namespace game {

/// The per-dungeon slot cap. The loader rejects a dungeon with more slots, and the save reader uses it as
/// the static upper bound of the `room` key — `room` is a battle ordinal, so it can never exceed slots - 1.
inline constexpr int kMaxDungeonSlots = 8;

/// One slot of a dungeon's room sequence (dungeon.json v2 `themes[].dungeons[].rooms[]`). `type` is the
/// open room-type key ("battle" | "event" — WP-09 widens the set), `family` names the room generator
/// template family: "open" (default) / "open_variant" (light) or "corridor" / "cover" / "split" (heavy;
/// loader-enforced to boss slots only, and only when `family_rules.boss_rooms_allow_heavy` is true —
/// D-23's data flip for WP-14), `boss` is "" | "mid" | "final" (battle slots only). `event_pool` is
/// consumed by the WP-09 event pick (the gap slot's pool selects the event); `encounter` (WP-14a) names a
/// fixed encounter for the slot ("" = the deterministic depth/theme pool pick — FrameRoomPlan).
struct FRunRoomSlot {
    std::string type;
    std::string family;
    std::string boss;
    std::string encounter;
    std::string eventPool;
};

struct FRunDungeonDef {
    std::string name;
    std::vector<FRunRoomSlot> rooms;
};

struct FRunThemeDef {
    std::string id;
    std::string name;
    std::vector<FRunDungeonDef> dungeons;
};

/// D-7 start support (`dungeon.json start_support`, consumed by WP-21's start-dungeon choice): materials
/// granted when a run starts below the checkpoint, and whether the first monster-room rune offer is
/// forced. All zeros when the block is absent (0 = off).
struct FRunStartSupport {
    int materialsPerDungeon{0};
    int materialsCap{0};
    int runeOffers{0};  // 0 = off, 1 = force the first monster-room offer
};

/// One row of the `cycles` table: the percents a single loop over the themes adds to enemy stats.
struct FRunCycleStep {
    int healthPct{0};
    int attackPct{0};
};

/// The summed cycle scale for a run's cycle count (WP-08: applied on top of the depth term in FCombatSheet
/// ③). One table entry repeats: cycle 2 with a single 10/10 row reads as 20/20. Zero for cycle 0 or an
/// empty table.
struct FCycleScale {
    int healthPct{0};
    int attackPct{0};
};

/// The boot-probe report: the unique (family, boss) tuples the loaded sequence authors and the room
/// generations the probe ran (tuples x seeds). Logged once as `[room] probe tuples=N iterations=M`.
struct FDungeonBootProbe {
    int tuples{0};
    int iterations{0};
};

/// The dungeon sequence: themes -> dungeons -> room slots (dungeon.json v2). Boot content: installed in the
/// registry context by InitializeDungeon. The run stores no position field: the live position is
/// `PositionOf(depth - 1)`, which equals `PositionOf(cleared)` outside camp (Camp: cleared == depth, but the
/// camp still belongs to dungeon depth - 1) — a cache field could go stale, a derivation cannot.
struct FRunSequence {
    std::vector<FRunThemeDef> themes;
    std::vector<FRunCycleStep> cycles;
    FRunStartSupport startSupport;
    /// `family_rules.boss_rooms_allow_heavy` (ship false until WP-14 decides the boss-room question, D-23
    /// §2): when true, a heavy family is legal on a boss slot. Never a silent coercion — the loader refuses
    /// the illegal combinations either way.
    bool bossRoomsAllowHeavy{false};
    /// Optional top-level `next_dungeon_hint` string (the camp view's conditional preview reads it straight
    /// from the document). WP-17/21 derive the next dungeon's name from `themes` when it is absent.
    std::optional<std::string> nextDungeonHint;

    /// `cleared` -> position. After the last dungeon the themes loop and `cycle` grows (D-19).
    struct FPosition {
        std::size_t theme{0};
        std::size_t dungeonInTheme{0};
        int cycle{0};
    };

    /// Parses and validates the sequence keys of a v2 dungeon document; on failure `error` names the first
    /// broken rule and the result is nullopt. Never throws.
    [[nodiscard]] static std::optional<FRunSequence> Load(const nlohmann::json& document, std::string& error);

    [[nodiscard]] FPosition PositionOf(int cleared) const;
    [[nodiscard]] const FRunDungeonDef& DungeonAt(const FPosition& position) const;
    [[nodiscard]] int TotalDungeons() const;
    /// Battle slots of the dungeon at `position` — the `room` upper bound (exclusive).
    [[nodiscard]] int BattleCountAt(const FPosition& position) const;
    /// The cycle count of a run that cleared `cleared` dungeons: cleared / TotalDungeons().
    [[nodiscard]] int RunCycle(int cleared) const;
    /// The N-th battle slot (0-based); nullptr when the ordinal is out of range.
    [[nodiscard]] const FRunRoomSlot* BattleSlotAt(const FPosition& position, int ordinal) const;
    /// True when the N-th battle slot names a boss (`boss` is "mid"/"final").
    [[nodiscard]] bool IsBossBattle(const FPosition& position, int ordinal) const;
    /// The single non-battle slot directly after the N-th battle (the gap event WP-09 fills), or nullptr
    /// when the next slot is a battle or the battle is the last one. The loader enforces at most one.
    [[nodiscard]] const FRunRoomSlot* GapEvent(const FPosition& position, int ordinal) const;
    /// The start-support materials for a run starting with `startIndex` cleared dungeons:
    /// min(index x materials_per_dungeon, materials_cap); 0 for index <= 0.
    [[nodiscard]] int StartMaterials(int startIndex) const;
};

/// The installed sequence (nullptr before InitializeDungeon).
[[nodiscard]] const FRunSequence* FindRunSequence(const entt::registry& registry);

/// The current run's position from `run.depth - 1` (never cached on the run). nullopt without a run or a
/// sequence — callers treat that as a wiring error, not as "dungeon 1".
[[nodiscard]] std::optional<FRunSequence::FPosition> RunPosition(const entt::registry& registry);

/// The cycle scale for the current run position. All zeros when the sequence or the run is missing, or the
/// cycle table is empty.
[[nodiscard]] FCycleScale CycleScaleFor(const entt::registry& registry);

/// WP-14a: the plan of battle `battleOrdinal` of the current dungeon (pure ctx read: FDungeonRun,
/// FRunSequence, FEncounterContent). `family` = the slot's effective family. `encounterId` = the slot's
/// named encounter or the deterministic pool pick (ResolveEncounter with the run seed, run depth and
/// `battleOrdinal`); "" for a boss slot (WP-15 generation pool), with no encounter table, without a run or
/// a sequence, or when nothing is eligible. Never writes ctx — the caller emplaces the plan.
[[nodiscard]] FRoomPlan FrameRoomPlan(const entt::registry& registry, int battleOrdinal);

} // namespace game
