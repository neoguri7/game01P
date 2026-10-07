#pragma once

#include "gameplay/data/FContentRegistry.h"
#include "gameplay/run/FPrototypeRun.h"

namespace game {

[[nodiscard]] bool RunPrototypeChecks(const gameplay::FContentRegistry& content, const FPrototypeRun& definition);

} // namespace game
