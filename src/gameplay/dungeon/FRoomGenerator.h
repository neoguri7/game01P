#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gameplay/grid/FGridTypes.h"

namespace game::gameplay {

struct FEnemyChoice {
    std::string unit;
    std::string displayName;
    int weight{1};
};

struct FIntRange {
    int min{0};
    int max{0};
};

/// WP-15: one boss part row of the room config (a `bosses.json` part resolved through the run slot). The
/// generator places `count` units of `unit` (display name `displayName`) in a boss room only; an empty
/// `FRoomGenConfig::bossParts` means no parts.
struct FBossPartSpawn {
    std::string unit;
    std::string displayName;
    int count{1};
};

/// WP-14a: one role slot of an encounter composition. The generator places exactly one unit of `units`
/// (weighted pick) for it; `role` is carried for the log/tests only (validated at boot, P14-5).
struct FRoleSlotSpawn {
    std::string role;
    std::vector<FEnemyChoice> units;
};

struct FRoomGenConfig {
    std::vector<std::string> names;
    std::vector<FEnemyChoice> enemies;
    std::string bossUnit;
    std::string bossName;
    FIntRange enemyCount;
    FIntRange obstacleCount;
    FIntRange trapCount;
    /// Room template family (WP-08): "open", "open_variant" (light), or "corridor" / "cover" / "split"
    /// (heavy). Missing/invalid names are refused by the sequence loader, so generation never sees one; the
    /// default keeps hand-built configs (tests) working. Deliberately placed before `bossParts`:
    /// pre-family aggregate initializers keep their meaning.
    std::string family{"open"};
    /// WP-15: boss parts the generator appends to a boss room's document (empty = none; non-boss rooms and
    /// part-less boss rooms output byte-identical rooms). Placed after `family` so pre-parts aggregate
    /// initializers keep their meaning.
    std::vector<FBossPartSpawn> bossParts;
    /// WP-14a: encounter composition (ApplyEncounter). Empty = the legacy weighted `enemies` pool, byte-
    /// identical output. Non-empty (non-boss rooms only): enemy i < roleSlots.size() is picked from
    /// roleSlots[i], the rest of `enemyCount` from `fill`. Appended last so older aggregate initializers
    /// keep their meaning.
    std::vector<FRoleSlotSpawn> roleSlots;
    std::vector<FEnemyChoice> fill;
};

/// The spawn band: every family keeps enemies inside these rows (the header contract, tested).
inline constexpr int kRoomEnemyFirstRow = 1;
inline constexpr int kRoomEnemyLastRow = 3;

/// A family's strength (D-23): light families may open normal monster rooms, heavy ones are reserved for
/// elite/boss rooms and stay gated by data until the elite gate lands (WP-14).
enum class EFamilyStrength { Light, Heavy };

/// The family -> strength table. The family set is generator code, not data: an unknown name is an error
/// (nullopt), the loader refuses it at boot.
[[nodiscard]] std::optional<EFamilyStrength> FamilyStrengthOf(std::string_view family);
[[nodiscard]] std::string_view FamilyStrengthName(EFamilyStrength strength);

struct FRoomRequest {
    std::uint32_t seed{1};
    int depth{1};
    int room{0};
    bool boss{false};
    // Party start cells; they and every enemy are guaranteed to stay mutually reachable.
    std::span<const std::pair<int, int>> partyCells;
    // P14-2 — the request extension: the SINGLE placement rule stays in GenerateRoom (no second placement
    // algorithm anywhere). All defaults = the pre-WP-14 output.
    std::string family;               ///< "" = config.family; otherwise this family (loader-valid name).
    int extraEnemyCount{0};           ///< Non-boss rooms: extra units appended after the composition through
                                      ///< the same spawn-slot list; shrinks (never fails) when slots run out.
    std::string extraEnemyUnit;       ///< Unit id of the extra enemies.
    std::string extraEnemyName;       ///< Display name of the extra enemies.
    std::string leaderNamePrefix;     ///< Non-boss rooms: prepended to the first enemy's (leader) name.
};

struct FGeneratedEnemy {
    std::string unit;
    std::string displayName;
    int x{0};
    int y{0};
};

struct FGeneratedRoom {
    static_assert(kGridWidth == kGridHeight, "generated rooms are square");
    static constexpr int kSize = kGridWidth;
    std::string name;
    std::array<std::string, kSize> terrain;
    std::vector<FGeneratedEnemy> enemies;
};

/// The emergent shape of a generated room (WP-08b, Smith & Whitehead): measured, never authored. A pure
/// function of the room + the party cells — no registry, no live terrain.
struct FRoomMetrics {
    int walls{0};              ///<'#' cells.
    int traps{0};              ///<'^' cells.
    double reachableRatio{0};  ///< Cells reachable from the party (walls block) / non-wall cells.
    int minPath{0};            ///< BFS steps from the party to the farthest enemy; other units block, the
                               ///< target itself does not. -1 = an enemy is unreachable (a broken room).
    int coverCells{0};         ///< Wall cells that block line of sight between at least one party member
                               ///< and one enemy (the game's supercover LoS rule).
};

/// Measures the emergent shape of a room. `partyCells` must lie on walkable cells (the caller's contract).
[[nodiscard]] FRoomMetrics MeasureRoom(const FGeneratedRoom& room,
                                       std::span<const std::pair<int, int>> partyCells);

/// Deterministic for a given config and request. Enemies spawn in the top rows (away from the party) in
/// every family — the spawn band (kRoomEnemyFirstRow..kRoomEnemyLastRow) is a fixed contract no layout
/// moves. `open` (light) scatters wall clusters and traps through the middle band, the classic layout;
/// `open_variant` (light) keeps the same wall budget but anchors the walls at the clutter-band corners,
/// the traps at the centre and the spawns on the flanks; the heavy families (`corridor`, `cover`, `split`)
/// build fixed structures that are not bound by `obstacleCount` and are reserved for elite/boss rooms
/// (loader-enforced, D-23). Every family keeps the invariants: the outer ring stays empty, walls never
/// disconnect any unit, and no unit spawns on a wall or trap. A boss room additionally appends
/// `config.bossParts` (WP-15) inside the spawn band, boss-adjacent first — the parts are the last enemies in
/// the document, and their placement keeps every unit connected (no random draws: a part-less boss room is
/// byte-identical to the pre-WP-15 output).
[[nodiscard]] FGeneratedRoom GenerateRoom(const FRoomGenConfig& config, const FRoomRequest& request);

} // namespace game::gameplay
