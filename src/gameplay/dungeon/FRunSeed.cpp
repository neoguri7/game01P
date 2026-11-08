#include "gameplay/dungeon/FRunSeed.h"

#include <cstdlib>
#include <string>

#include "core/Logger.h"

namespace game {
namespace {

constexpr const char* kSeedEnv = "GAME01P_SEED";  // boundary: env key

/// The parsed env seed, or nullopt when unset/empty/without a valid value. An invalid value is reported
/// exactly once per process (a retry would repeat it otherwise and drown the log).
std::optional<std::uint32_t> ReadEnvSeed() {
    static bool warnedInvalid = false;
    const char* value = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    value = std::getenv(kSeedEnv);
    if (value == nullptr || *value == '\0') return std::nullopt;
    const std::optional<std::uint32_t> parsed = ParseRunSeed(value);
    if (!parsed && !warnedInvalid) {
        warnedInvalid = true;
        LOG_WARN("{}='{}' is not a valid run seed (decimal u32); falling back to the drawn seed.", kSeedEnv, value);
    }
    return parsed;
}

} // namespace

std::optional<std::uint32_t> ParseRunSeed(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::uint64_t value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') return std::nullopt;  // no sign, no whitespace, no grouping
        value = value * 10 + static_cast<std::uint64_t>(ch - '0');
        if (value > 0xFFFFFFFFull) return std::nullopt;  // overflow — never wraps into a different seed
    }
    return static_cast<std::uint32_t>(value);
}

std::uint32_t RunSeedFor(std::optional<std::uint32_t> envSeed, std::uint32_t fallback, std::uint32_t attempt) {
    return envSeed ? *envSeed + attempt : fallback;  // unsigned wrap near the u32 ceiling is intended
}

std::uint32_t ResolveRunSeed(std::uint32_t fallback) {
    static std::uint32_t attempt = 0;  // per-process resolve count; the first start is attempt 0
    const std::optional<std::uint32_t> envSeed = ReadEnvSeed();
    const std::uint32_t seed = RunSeedFor(envSeed, fallback, attempt);
    if (envSeed) {
        LOG_INFO("[seed] {}={} attempt={} -> {}", kSeedEnv, *envSeed, attempt, seed);
    }
    ++attempt;
    return seed;
}

} // namespace game
