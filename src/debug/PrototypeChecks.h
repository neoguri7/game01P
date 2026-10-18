#pragma once

#include "gameplay/ai/FAiLibrary.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/run/FPrototypeRun.h"

namespace game {

[[nodiscard]] bool RunPrototypeChecks(
    const gameplay::FContentRegistry& content, const ai::FAiLibrary& aiLibrary, const FPrototypeRun& definition);

} // namespace game
