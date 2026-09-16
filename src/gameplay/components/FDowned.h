#pragma once

namespace game::gameplay {

/// Exhausted unit: it occupies its cell, is skipped by the turn order, and cannot act. Set once by the
/// damage path. why no revival flag: design §4 확정 — 부상·사망 없음, and a battle never returns a downed
/// unit to play, so a reversible tag would be a state this slice cannot reach (R11).
struct FDowned {};

} // namespace game::gameplay
