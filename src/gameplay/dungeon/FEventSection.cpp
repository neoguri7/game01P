#include "gameplay/dungeon/FEventSection.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FRunPath.h"
#include "gameplay/dungeon/FRunSequence.h"

namespace game {
namespace {

/// The event-pick hash constants (WP-09 §4) — a family of their own: FRunSeed and FRoomGenerator mix their
/// own constants (FRandom: 0x9E3779B9 / 0x21F0AAAD / 0x735A2D97), so a room's generation and its event pick
/// never move each other. The three key factors are distinct fixed odd 64-bit values, one per key term;
/// changing one changes every event a run draws.
constexpr std::uint64_t kEventPickSalt = 0x9E3779B97F4A7C15ull;        // the event-only salt (splitmix64 gamma)
constexpr std::uint64_t kEventPickSeedFactor = 0xA24BAED4963EE407ull;  // key terms: seed / depth / room
constexpr std::uint64_t kEventPickDepthFactor = 0x9FB21C651E98DF25ull;
constexpr std::uint64_t kEventPickRoomFactor = 0x2545F4914F6CDD1Dull;

/// The splitmix64 finalizer's own two multipliers (the 30/27/31 shift mix). Only the finalizer must stay
/// bit-identical to the reference mix — toolchain-stable on purpose, no std distribution (WP-09 §4).
constexpr std::uint64_t kSplitMixMul1 = 0xBF58476D1CE4E5B9ull;
constexpr std::uint64_t kSplitMixMul2 = 0x94D049BB133111EBull;

std::uint64_t FinalizeEventPick(std::uint64_t value) {
    value = (value ^ (value >> 30)) * kSplitMixMul1;
    value = (value ^ (value >> 27)) * kSplitMixMul2;
    return value ^ (value >> 31);
}

/// Reads one `seen` array of strings. False = malformed (not an array, or holds a non-string).
bool ReadSeen(const nlohmann::json& value, std::vector<std::string>& destination) {
    if (!value.is_array()) return false;
    for (const auto& entry : value) {
        if (!entry.is_string()) return false;
        destination.push_back(entry.get<std::string>());
    }
    return true;
}

nlohmann::json PackEvents(entt::registry& registry) {
    nlohmann::json section = nlohmann::json::object();
    if (const auto* state = registry.ctx().find<FEventRunState>(); state != nullptr && !state->seen.empty()) {
        section["seen"] = state->seen;
    }
    // Only a resolved instance is worth storing: an unresolved one is re-derived from the same
    // (seed, depth, room, seen) pick, which is the whole point of the derived-object design.
    if (const auto* instance = registry.ctx().find<FEventInstance>(); instance != nullptr && instance->resolved) {
        section["event_id"] = instance->eventId;
        section["choice"] = instance->choice;
    }
    return section;
}

/// Forgiving like the camp/offers sections: a malformed section resets this module's state (no instance,
/// empty seen) instead of failing the whole load; an absent section ({} from FSaveSections) is the module
/// default. Never reads FDungeonRun — on the continue path the section is unpacked before the run is
/// installed.
void UnpackEvents(entt::registry& registry, const nlohmann::json& section) {
    std::vector<std::string> seen;
    FEventInstance instance;
    bool haveInstance = false;
    bool corrupt = false;

    if (!section.is_object()) {
        corrupt = true;  // only reachable through a direct call; FSaveSections already refuses this
    } else if (!section.empty()) {
        if (const auto found = section.find("seen"); found != section.end()) {
            if (!ReadSeen(*found, seen)) corrupt = true;
        }
        if (!corrupt) {
            const auto eventId = section.find("event_id");
            const auto choice = section.find("choice");
            if (eventId != section.end() || choice != section.end()) {
                // The pair is packed together and read together: a lone event_id or choice is malformed.
                if (eventId == section.end() || !eventId->is_string() || choice == section.end() ||
                    !choice->is_number_integer()) {
                    corrupt = true;
                } else {
                    // nlohmann integer values may be unsigned; check Int64 and never narrow blindly.
                    std::optional<int> parsed;
                    if (choice->is_number_unsigned()) {
                        const auto value = choice->get<std::uint64_t>();
                        if (value <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                            parsed = static_cast<int>(value);
                        }
                    } else if (const auto value = choice->get<std::int64_t>(); value >= 0) {
                        parsed = static_cast<int>(value);
                    }
                    if (!parsed) {
                        corrupt = true;
                    } else {
                        instance.eventId = eventId->get<std::string>();
                        instance.choice = *parsed;
                        instance.resolved = true;
                        haveInstance = true;
                    }
                }
            }
        }
    }

    if (corrupt) {
        LOG_WARN("The saved event state is unusable; it resets (no pending event, empty seen).");
        seen.clear();
        haveInstance = false;
    }
    registry.ctx().erase<FEventInstance>();
    registry.ctx().erase<FEventRunState>();
    if (haveInstance) registry.ctx().emplace<FEventInstance>(std::move(instance));
    if (!seen.empty()) registry.ctx().emplace<FEventRunState>(FEventRunState{std::move(seen)});
}

} // namespace

FSaveSectionSerializer EventSaveSection() {
    FSaveSectionSerializer serializer;
    serializer.id = std::string(kEventSectionId);
    serializer.version = kEventSectionVersion;
    serializer.pack = PackEvents;
    serializer.unpack = UnpackEvents;
    return serializer;
}

void ResetEventsForRun(entt::registry& registry) {
    registry.ctx().erase<FEventInstance>();
    if (auto* state = registry.ctx().find<FEventRunState>(); state != nullptr) {
        state->seen.clear();  // kept, not erased: the module state stays present and empty
    }
}

void MarkEventSeen(entt::registry& registry, std::string_view eventId) {
    auto* state = registry.ctx().find<FEventRunState>();
    if (state == nullptr) state = &registry.ctx().emplace<FEventRunState>();
    if (std::ranges::find(state->seen, eventId) != state->seen.end()) return;  // no duplicates
    state->seen.emplace_back(eventId);
}

std::vector<std::string> EventSeen(const entt::registry& registry) {
    const auto* state = registry.ctx().find<FEventRunState>();
    return state != nullptr ? state->seen : std::vector<std::string>{};
}

std::uint64_t EventPickHash(std::uint32_t seed, int depth, int room) {
    std::uint64_t key = kEventPickSalt;
    key ^= static_cast<std::uint64_t>(seed) * kEventPickSeedFactor;
    key ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(depth)) * kEventPickDepthFactor;
    key ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(room)) * kEventPickRoomFactor;
    return FinalizeEventPick(key);
}

std::optional<std::string> PickEventId(std::span<const FEventDef> catalog, std::string_view pool,
                                       std::span<const std::string> seen, std::uint32_t seed, int depth,
                                       int room) {
    std::vector<const FEventDef*> members;  // the pool in catalog order: the seed first, then file order
    for (const auto& event : catalog) {
        if (EventInPool(event, pool)) members.push_back(&event);
    }
    if (members.empty()) return std::nullopt;  // an empty pool: nothing to pick
    std::vector<const FEventDef*> candidates;
    candidates.reserve(members.size());
    for (const auto* event : members) {
        if (std::ranges::find(seen, event->id) == seen.end()) candidates.push_back(event);
    }
    // The pool is exhausted: the whole pool is allowed again (a reshuffle; `seen` is never cleared here).
    if (candidates.empty()) candidates = std::move(members);
    const auto index = static_cast<std::size_t>(EventPickHash(seed, depth, room) % candidates.size());
    return candidates[index]->id;
}

std::optional<FEventInstance> CurrentEventInstance(entt::registry& registry) {
    if (const auto* instance = registry.ctx().find<FEventInstance>(); instance != nullptr) return *instance;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr || run->phase != EDungeonPhase::Event) return std::nullopt;
    const auto* sequence = FindRunSequence(registry);
    if (sequence == nullptr) return std::nullopt;  // a wiring error the callers report
    const auto position = sequence->PositionOf(run->depth - 1);
    const auto* slot = sequence->GapEvent(position, run->room);
    if (slot == nullptr) return std::nullopt;  // this battle has no gap event
    const std::vector<std::string> pools = GapEventPools(registry, run->room);
    if (pools.empty()) return std::nullopt;
    const std::vector<FEventDef> catalog = EventCatalog(registry);
    std::optional<std::string> id;
    if (pools.size() == 1) {
        id = PickEventId(catalog, pools[0], EventSeen(registry), run->seed, run->depth, run->room);
    } else {
        // WP-41: a chosen "?" fork draws from the slot pool and the mystery pool together.
        std::vector<FEventDef> mixed;
        for (const auto& event : catalog) {
            if (std::ranges::any_of(pools, [&](const std::string& pool) { return EventInPool(event, pool); })) {
                mixed.push_back(event);
            }
        }
        id = PickEventId(mixed, "", EventSeen(registry), run->seed, run->depth, run->room);
    }
    if (!id) return std::nullopt;  // the slot's pool is empty
    return FEventInstance{*id, -1, false};
}

} // namespace game
