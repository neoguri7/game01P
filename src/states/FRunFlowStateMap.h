#pragma once

#include <string_view>

#include "states/FBaseState.h"
#include "gameplay/dungeon/FRunFlow.h"

namespace game {

/// The one place a run-flow result becomes an app-layer stack transition (WP-02b).
///
/// `topId` is the catalog id of the state that applied the result. A run modal result is applied by the
/// modal host itself (topId = the modal id), a battle result by "ember.battle" (topId = kEmberBattleId).
/// A ShowModal from an unknown top is a wiring bug: it logs and returns None, so no state is invented.
[[nodiscard]] FStateTransition ToStateTransition(const FRunResult& result, std::string_view topId);

} // namespace game
