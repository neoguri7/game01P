#pragma once

#include <entt/entt.hpp>

namespace game {

/// The `run.ext.log` save section id (WP-19a / WP-07 B4).
inline constexpr const char* kRunLogSectionId = "log";

/// Registers the log save section: pack keeps the run's `log_id`/`run_started_at`, unpack re-attaches the
/// run log to its existing JSONL file (the whole history reloads, the next write appends to the same file).
/// Called once from InitializeRunLog.
void RegisterRunLogSection(entt::registry& registry);

} // namespace game
