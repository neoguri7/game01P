#include "gameplay/data/FEncounterContent.h"

namespace game::gameplay {

FEncounterContent decodeEncounter(const FContentRow& row) {
    FEncounterContent encounter;
    encounter.id = row.text(kEncounterFieldId);
    encounter.displayName = row.text(kEncounterFieldDisplayName);
    encounter.partyIds = row.textArray(kEncounterFieldParty);
    encounter.enemyIds = row.textArray(kEncounterFieldEnemies);
    encounter.partyCells = row.numberArray(kEncounterFieldPartyCells);
    encounter.enemyCells = row.numberArray(kEncounterFieldEnemyCells);

    // 미결(design §4): 그리드 최종 크기 — design §4는 8x8을 확정했고, encounter 행이 그 값을 덮어쓴다.
    encounter.gridWidth = row.integer(kEncounterFieldGridWidth, 8);   // fallback: 8 (design §4 확정 8x8)
    encounter.gridHeight = row.integer(kEncounterFieldGridHeight, 8); // fallback: 8 (design §4 확정 8x8)
    return encounter;
}

} // namespace game::gameplay
