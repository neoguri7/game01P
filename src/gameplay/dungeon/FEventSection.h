#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FSaveSections.h"

namespace game {

/// `run.ext.event` (WP-07 section, id "event", version 1).
inline constexpr std::string_view kEventSectionId = "event";
inline constexpr int kEventSectionVersion = 1;

/// Run-scoped event memory: the ids resolved in this run (no duplicates, resolve order). Ctx-owned, saved in
/// the event section, cleared by a new run. Kept apart from FEventInstance so it survives ContinueEvent.
struct FEventRunState {
    std::vector<std::string> seen;
};

/// The event room in progress. Ctx-owned, a derived object: an unresolved instance is re-derived from the
/// pick (same seed/depth/room/seen = same event); a resolved one is restored from the event section so a
/// continued run reopens the result screen, never the choices (no double payment).
struct FEventInstance {
    std::string eventId;
    int choice{-1};
    bool resolved{false};
};

/// The event section serializer. pack: `{}` when there is nothing to keep; `seen` when non-empty;
/// `event_id` + `choice` only while a resolved FEventInstance exists. unpack: `{}` / absent = no instance +
/// empty seen (additive: old saves and fresh boots behave as before). A malformed section (wrong types) is
/// logged and resets the module state (the camp/offers forgiving policy); a seen id that is no longer in the
/// catalog is kept and simply never matches (not corruption).
[[nodiscard]] FSaveSectionSerializer EventSaveSection();

/// New run: clears FEventRunState.seen and erases FEventInstance.
void ResetEventsForRun(entt::registry& registry);

/// Adds the id to FEventRunState.seen once (creating the state when absent).
void MarkEventSeen(entt::registry& registry, std::string_view eventId);

/// The run's seen list (empty when the state is absent).
[[nodiscard]] std::vector<std::string> EventSeen(const entt::registry& registry);

/// Event-pick integer hash: splitmix64-family mixing of (seed, depth, room) with a fixed event-only salt
/// (independent of the room-generation hash). Toolchain-stable — no std distribution.
[[nodiscard]] std::uint64_t EventPickHash(std::uint32_t seed, int depth, int room);

/// The pick (pure): pool members = catalog events with EventInPool(pool), in catalog order; candidates =
/// pool members whose id is not in `seen`; when none remain the whole pool is allowed again (seen is never
/// cleared here). index = EventPickHash(seed, depth, room) % candidates.size(). nullopt = empty pool.
[[nodiscard]] std::optional<std::string> PickEventId(std::span<const FEventDef> catalog, std::string_view pool,
                                                     std::span<const std::string> seen, std::uint32_t seed,
                                                     int depth, int room);

/// The current event room as the view and the flow see it: the ctx FEventInstance when present; otherwise,
/// when the run is in the Event phase, an unresolved instance derived from the pick for the gap-event slot
/// after battle `run.room` (pool = that slot's eventPool) and EventSeen. nullopt when there is no run, the
/// phase is not Event, there is no such slot, or the pool is empty. Never mutates the registry's ctx.
[[nodiscard]] std::optional<FEventInstance> CurrentEventInstance(entt::registry& registry);

} // namespace game
