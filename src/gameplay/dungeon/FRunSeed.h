#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace game {

/// WP-28 A3: reproducible runs. `GAME01P_SEED=<u32>` (decimal) overrides the run seed the ember states
/// otherwise draw from SDL_GetTicks(), so the measurement harness can replay a run.
///
/// ParseRunSeed is the pure syntax half: digits only, no sign and no whitespace, empty/junk/overflow ->
/// nullopt. "0" is a valid seed (a fixed value, not "unset").
[[nodiscard]] std::optional<std::uint32_t> ParseRunSeed(std::string_view text);

/// RunSeedFor is the pure sequencing half: the env seed advances by one attempt per resolve inside the
/// process (u32 wrap intended), so the first start of a process uses the seed itself and a
/// retry-from-checkpoint uses seed + 1, ...; without the env the fallback (fresh ticks) is returned as-is.
[[nodiscard]] std::uint32_t RunSeedFor(
    std::optional<std::uint32_t> envSeed, std::uint32_t fallback, std::uint32_t attempt);

/// The run seed for one start. Reads GAME01P_SEED with std::getenv (not SDL_getenv: SDL caches the
/// environment, so a test that sets the variable later would not be seen — same reason as GAME01P_SAVE_DIR),
/// warns once on an invalid value and falls back, and keeps a file-local attempt counter: the n-th
/// ResolveRunSeed call of the process carries attempt n (0-based). With the env set the first start gets
/// the seed and a retry-from-checkpoint gets seed + 1; without it every call draws a fresh fallback.
/// Logs `[seed] GAME01P_SEED=<v> attempt=<n> -> <seed>` whenever the env seed is used.
[[nodiscard]] std::uint32_t ResolveRunSeed(std::uint32_t fallback);

} // namespace game
