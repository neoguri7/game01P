#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace game {

/// What happened to a run. The kinds are the run-level milestones; the views that award them (reward,
/// camp, boss) append them without owning the log. Order matters to consumers: the tail keeps the
/// append order and `seq` is strictly increasing per run.
enum class ERunEventKind {
    RoomCleared,
    DungeonCleared,
    BossDefeated,
    RunEnded,
    RewardPicked,
    CampSpent,
    EventChosen,
    ForkChosen
};

/// The recent-events ring size. Only the last 64 events are kept; a consumer that needs history reads
/// its own state (the log is a notification trail, not the source of truth).
inline constexpr std::size_t kRunEventTail = 64;

struct FRunEvent {
    ERunEventKind kind{ERunEventKind::RoomCleared};
    std::uint64_t seq{0};
    int depth{0};
    int room{0};
    std::string id;
    int amount{0};
    /// Which party member the event belongs to. -1 = party-wide / unknown (the forward-compatible default
    /// WP-10's camp purchases and WP-19a's camp lines rely on).
    int member{-1};
};

/// Per-run: a new run (start or continue) gets a fresh log, so the log never survives a run on purpose.
/// It is never saved — a section's state is the source of truth, not this trail.
struct FRunEventLog {
    std::vector<FRunEvent> tail;
    std::uint64_t nextSeq{1};
};

/// Context-owned and registered once. Listeners outlive run recreation (a new run keeps the subscribers).
struct FRunEventListeners {
    std::vector<std::function<void(const FRunEvent&)>> listeners;
    bool delivering{false};
};

/// Appends one event to the current run's log and delivers it synchronously to the listeners in
/// registration order. An append while a listener is delivering is a re-entrancy bug: it is logged and
/// dropped (the caller's event is not appended). A throwing listener is logged and the next listener
/// still runs. A run with no log gets one (depth/room are 0 while no run exists).
void AppendRunEvent(
    entt::registry& registry, ERunEventKind kind, std::string id = {}, int amount = 0, int member = -1);

/// Registers a listener. The listener vector lives in the registry context and survives a run restart.
void RegisterRunEventListener(entt::registry& registry, std::function<void(const FRunEvent&)> listener);

/// Erases the per-run log and the listener list (composition-root shutdown).
void ShutdownRunEventLog(entt::registry& registry);

} // namespace game
