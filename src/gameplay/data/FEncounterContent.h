#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>
#include <vector>

namespace game::gameplay {

/// One battle as authored in `assets/data/encounters.json`: who fights, where each unit stands, and how big
/// the grid is. why data: design §4 fixes "그리드 = 8x8" and "사거리는 칸 단위", so both the grid size and
/// every spawn cell are balance values — retuning a fight must not need a recompile (R4/R22).
/// 룸/던전 배치(design §3)와 몬스터 AI(미결(design §4))는 이 슬라이스에 없다: encounter = 전투 1회분이다.
struct FEncounterContent {
    std::string id;
    std::string displayName;
    std::vector<std::string> partyIds; // player-side unit rows, spawn order == cell order
    std::vector<std::string> enemyIds; // enemy-side unit rows
    std::vector<double> partyCells;    // flat [x, y, x, y, ...] — 칸 좌표는 밸런스 값이다
    std::vector<double> enemyCells;
    int gridWidth{8};  // fallback: design §4 확정 8x8 — the row overrides it
    int gridHeight{8}; // fallback: design §4 확정 8x8 — the row overrides it
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kEncounterField* indices below, decodeEncounter and
/// assets/data/encounters.json must keep the same order and the same keys.
inline constexpr FContentField kEncounterFields[] = {
    {"id", EContentFieldType::Text, true},           // boundary: asset key
    {"display_name", EContentFieldType::Text, true}, // boundary: asset key
    {"party", EContentFieldType::IdArray, false},    // boundary: asset key
    {"enemies", EContentFieldType::IdArray, false},  // boundary: asset key
    {"party_cells", EContentFieldType::NumberArray, false}, // boundary: asset key
    {"enemy_cells", EContentFieldType::NumberArray, false}, // boundary: asset key
    {"grid_width", EContentFieldType::Number, false},       // boundary: asset key
    {"grid_height", EContentFieldType::Number, false},      // boundary: asset key
};

inline constexpr std::size_t kEncounterFieldId = 0;
inline constexpr std::size_t kEncounterFieldDisplayName = 1;
inline constexpr std::size_t kEncounterFieldParty = 2;
inline constexpr std::size_t kEncounterFieldEnemies = 3;
inline constexpr std::size_t kEncounterFieldPartyCells = 4;
inline constexpr std::size_t kEncounterFieldEnemyCells = 5;
inline constexpr std::size_t kEncounterFieldGridWidth = 6;
inline constexpr std::size_t kEncounterFieldGridHeight = 7;

static_assert(std::size(kEncounterFields) == 8, "kEncounterFields and the kEncounterField* indices drifted apart");

/// Projects one validated row into an encounter. Cannot fail on types (R22): the loader already rejected a
/// wrong type or a missing required key. Reference and cell-count checks belong to FContentRegistry, which
/// is the one place that owns every table at once.
[[nodiscard]] FEncounterContent decodeEncounter(const FContentRow& row);

} // namespace game::gameplay
