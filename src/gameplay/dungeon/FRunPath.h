#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FRunSequence.h"

namespace game {

inline constexpr std::string_view kRunPathSectionId = "path";
inline constexpr std::string_view kEventPoolMystery = "mystery";
inline constexpr int kForkSlotSalt = 1001;
inline constexpr int kForkAffixSalt = 1002;

/// The current dungeon's fork and its decision (ctx). Derived once per (seed, depth), then only read; saved
/// whole through the `path` section, so a load never re-derives it.
struct FRunPathState {
    std::uint32_t seed{0};
    int depth{0};             // 0 = nothing derived yet
    bool hasFork{false};
    int slot{0};              // index into FRunDungeonDef::rooms
    bool battle{false};       // true: battle vs elite; false: event vs "?"
    int battleOrdinal{0};     // battle fork: the slot's battle ordinal; event fork: the ordinal of the battle before it
    std::string affixId;      // battle forks only
    int choice{-1};           // -1 undecided, 0 standard, 1 variant
};

enum class EPathNodeKind { Battle, Elite, Event, Mystery, Boss, Camp };

struct FPathNode {
    EPathNodeKind kind{EPathNodeKind::Battle};
    std::string label;        // "Battle" | "Elite: <display_name>" | "Event" | "?" | "Boss: <name>" | "Boss" | "Camp"
    bool done{false};
    bool current{false};
};

struct FPathStrip {
    std::vector<FPathNode> nodes;      // one per room slot, then Camp
    int forkNode{-1};                  // index into nodes, -1 = no fork
    std::optional<FPathNode> forkAlt;  // the variant node while the fork is undecided
};

struct FForkOption {
    std::string title;
    std::string detail;
};

/// Registers the `path` save section and emplaces the ctx state. The state is never erased afterwards, so
/// EnsureRunPath never inserts into the context (pointers and references into ctx stay valid across it).
void RegisterRunPathSection(entt::registry& registry);
/// Resets the state to default (depth 0), so the next EnsureRunPath derives again. Called at run start.
void ResetRunPath(entt::registry& registry);
/// The state for the installed run. Derives the fork when the state's seed or depth differs from the run's;
/// otherwise validates the stored fork against the sequence and drops it (hasFork = false) on a mismatch.
[[nodiscard]] FRunPathState& EnsureRunPath(entt::registry& registry);
/// The room-slot index the run is in: Battle/Reward -> the slot of battle `run.room`; Event -> that slot + 1;
/// Camp -> rooms.size(). nullopt without run or sequence, or in Defeat.
[[nodiscard]] std::optional<int> CurrentSlotIndex(const entt::registry& registry);
/// True when a fork exists, is undecided, the phase is Reward or Event, and state.slot == CurrentSlotIndex + 1.
[[nodiscard]] bool IsForkPending(entt::registry& registry);
/// Decides the pending fork (0 or 1) and appends the ForkChosen run event. False when nothing is pending.
bool DecideFork(entt::registry& registry, int choice);
[[nodiscard]] bool IsEliteBattle(entt::registry& registry, int battleOrdinal);
[[nodiscard]] const FEliteAffix* EliteAffixFor(entt::registry& registry, int battleOrdinal);  // nullptr = not elite
/// The pools the gap event after battle `battleOrdinal` draws from: {slot pool, "mystery"} for a chosen "?"
/// fork, else {slot pool}; empty when there is no gap slot.
[[nodiscard]] std::vector<std::string> GapEventPools(entt::registry& registry, int battleOrdinal);
[[nodiscard]] FPathStrip BuildPathStrip(entt::registry& registry);
/// [0] standard, [1] variant; empty when no fork is pending.
[[nodiscard]] std::vector<FForkOption> ForkOptions(entt::registry& registry);

} // namespace game
