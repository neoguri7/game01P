#include "gameplay/dungeon/FRunPath.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "gameplay/combat/FEliteRules.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FEventSection.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FSaveSections.h"

namespace game {
namespace {

nlohmann::json PackPath(entt::registry& registry) {
    nlohmann::json section = nlohmann::json::object();
    const auto* state = registry.ctx().find<FRunPathState>();
    if (state == nullptr) return section;
    section["seed"] = state->seed;
    section["depth"] = state->depth;
    section["has_fork"] = state->hasFork;
    section["slot"] = state->slot;
    section["battle"] = state->battle;
    section["battle_ordinal"] = state->battleOrdinal;
    section["affix_id"] = state->affixId;
    section["choice"] = state->choice;
    return section;
}

/// Reads one integer key within [low, high]; false = missing, mistyped or out of range.
bool ReadBoundedInt(const nlohmann::json& section, const char* key, std::int64_t low, std::int64_t high,
                    std::int64_t& out) {
    const auto found = section.find(key);
    if (found == section.end() || !found->is_number_integer()) return false;
    if (found->is_number_unsigned()) {
        const auto value = found->get<std::uint64_t>();
        if (value > static_cast<std::uint64_t>(high)) return false;
        out = static_cast<std::int64_t>(value);
    } else {
        out = found->get<std::int64_t>();
    }
    return out >= low && out <= high;
}

bool ParsePath(const nlohmann::json& section, FRunPathState& out) {
    if (!section.is_object()) return false;
    std::int64_t seed = 0;
    std::int64_t depth = 0;
    std::int64_t slot = 0;
    std::int64_t ordinal = 0;
    std::int64_t choice = 0;
    constexpr std::int64_t kIntMax = std::numeric_limits<int>::max();
    if (!ReadBoundedInt(section, "seed", 0, std::numeric_limits<std::uint32_t>::max(), seed)) return false;
    if (!ReadBoundedInt(section, "depth", 0, kIntMax, depth)) return false;
    if (!ReadBoundedInt(section, "slot", 0, kIntMax, slot)) return false;
    if (!ReadBoundedInt(section, "battle_ordinal", 0, kIntMax, ordinal)) return false;
    if (!ReadBoundedInt(section, "choice", -1, 1, choice)) return false;
    const auto hasFork = section.find("has_fork");
    const auto battle = section.find("battle");
    const auto affix = section.find("affix_id");
    if (hasFork == section.end() || !hasFork->is_boolean()) return false;
    if (battle == section.end() || !battle->is_boolean()) return false;
    if (affix == section.end() || !affix->is_string()) return false;
    out.seed = static_cast<std::uint32_t>(seed);
    out.depth = static_cast<int>(depth);
    out.hasFork = hasFork->get<bool>();
    out.slot = static_cast<int>(slot);
    out.battle = battle->get<bool>();
    out.battleOrdinal = static_cast<int>(ordinal);
    out.affixId = affix->get<std::string>();
    out.choice = static_cast<int>(choice);
    return true;
}

/// Never reads FDungeonRun: on the continue path the section is unpacked before the run is installed.
void UnpackPath(entt::registry& registry, const nlohmann::json& section) {
    auto* state = registry.ctx().find<FRunPathState>();
    if (state == nullptr) return;
    FRunPathState parsed;
    // `section_version` is left in the object by the framework; ParsePath ignores unknown keys.
    if (section.is_object() && !section.empty()) {
        if (!ParsePath(section, parsed)) {
            LOG_WARN("The saved path state is unusable; it resets.");
            parsed = FRunPathState{};
        }
    }
    *state = std::move(parsed);
}

const FEliteAffix* FindAffix(const FRunSequence& sequence, const std::string& id) {
    for (const auto& affix : sequence.elite.affixes) {
        if (affix.id == id) return &affix;
    }
    return nullptr;
}

int BattlesBefore(const std::vector<FRunRoomSlot>& rooms, int slot) {
    int count = 0;
    for (int index = 0; index < slot && index < static_cast<int>(rooms.size()); ++index) {
        if (rooms[static_cast<std::size_t>(index)].type == "battle") ++count;
    }
    return count;
}

} // namespace

void RegisterRunPathSection(entt::registry& registry) {
    if (registry.ctx().find<FRunPathState>() == nullptr) registry.ctx().emplace<FRunPathState>();
    FSaveSectionSerializer serializer;
    serializer.id = std::string(kRunPathSectionId);
    serializer.version = 1;
    serializer.pack = PackPath;
    serializer.unpack = UnpackPath;
    RegisterSaveSection(registry, std::move(serializer));
}

void ResetRunPath(entt::registry& registry) {
    if (auto* state = registry.ctx().find<FRunPathState>(); state != nullptr) *state = FRunPathState{};
}

FRunPathState& EnsureRunPath(entt::registry& registry) {
    auto* statePtr = registry.ctx().find<FRunPathState>();
    assert(statePtr != nullptr && "FRunPathState is registered at boot");
    FRunPathState& state = *statePtr;
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* sequence = FindRunSequence(registry);
    if (run == nullptr || sequence == nullptr) return state;
    const auto& rooms = sequence->DungeonAt(sequence->PositionOf(run->depth - 1)).rooms;

    if (state.seed != run->seed || state.depth != run->depth) {
        const bool hasMystery = [&] {
            const std::vector<FEventDef> catalog = EventCatalog(registry);
            return std::ranges::any_of(catalog, [](const FEventDef& event) {
                return EventInPool(event, kEventPoolMystery);
            });
        }();
        std::vector<int> eligible;
        for (int index = 1; index < static_cast<int>(rooms.size()); ++index) {
            const auto& room = rooms[static_cast<std::size_t>(index)];
            const bool ok = room.type == "battle" ? (room.boss.empty() && !sequence->elite.affixes.empty())
                                                  : hasMystery;
            if (ok) eligible.push_back(index);
        }
        state = FRunPathState{};
        state.seed = run->seed;
        state.depth = run->depth;
        if (!eligible.empty()) {
            state.hasFork = true;
            state.slot = eligible[EventPickHash(run->seed, run->depth, kForkSlotSalt) % eligible.size()];
            state.battle = rooms[static_cast<std::size_t>(state.slot)].type == "battle";
            const int before = BattlesBefore(rooms, state.slot);
            state.battleOrdinal = state.battle ? before : before - 1;
            if (state.battle) {
                const auto& affixes = sequence->elite.affixes;
                state.affixId = affixes[EventPickHash(run->seed, run->depth, kForkAffixSalt) % affixes.size()].id;
            }
        }
        return state;
    }

    if (state.hasFork) {
        bool valid = state.slot >= 0 && state.slot < static_cast<int>(rooms.size());
        if (valid) {
            const auto& room = rooms[static_cast<std::size_t>(state.slot)];
            valid = (room.type == "battle") == state.battle;
            if (valid && state.battle) {
                valid = room.boss.empty() && FindAffix(*sequence, state.affixId) != nullptr;
            }
        }
        if (!valid) {
            state.hasFork = false;
            LOG_WARN("Content changed under this save: the fork of depth {} is dropped.", state.depth);
        }
    }
    return state;
}

std::optional<int> CurrentSlotIndex(const entt::registry& registry) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* sequence = FindRunSequence(registry);
    if (run == nullptr || sequence == nullptr || run->phase == EDungeonPhase::Defeat) return std::nullopt;
    const auto& rooms = sequence->DungeonAt(sequence->PositionOf(run->depth - 1)).rooms;
    if (run->phase == EDungeonPhase::Camp) return static_cast<int>(rooms.size());
    int seen = 0;
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        if (rooms[index].type != "battle") continue;
        if (seen == run->room) {
            return static_cast<int>(index) + (run->phase == EDungeonPhase::Event ? 1 : 0);
        }
        ++seen;
    }
    return std::nullopt;
}

bool IsForkPending(entt::registry& registry) {
    if (registry.ctx().find<FRunPathState>() == nullptr) return false;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr || FindRunSequence(registry) == nullptr) return false;
    if (run->phase != EDungeonPhase::Reward && run->phase != EDungeonPhase::Event) return false;
    const FRunPathState& state = EnsureRunPath(registry);
    if (!state.hasFork || state.choice != -1) return false;
    const auto current = CurrentSlotIndex(registry);
    return current.has_value() && *current + 1 == state.slot;
}

bool DecideFork(entt::registry& registry, int choice) {
    if (!IsForkPending(registry)) return false;
    FRunPathState& state = EnsureRunPath(registry);
    state.choice = choice != 0 ? 1 : 0;
    std::string kind;
    if (state.choice == 0) {
        kind = state.battle ? "battle" : "event";
    } else {
        kind = state.battle ? "elite:" + state.affixId : "mystery";
    }
    AppendRunEvent(registry, ERunEventKind::ForkChosen, std::move(kind), state.choice);
    return true;
}

bool IsEliteBattle(entt::registry& registry, int battleOrdinal) {
    if (registry.ctx().find<FRunPathState>() == nullptr) return false;
    const FRunPathState& state = EnsureRunPath(registry);
    return state.hasFork && state.battle && state.choice == 1 && state.battleOrdinal == battleOrdinal;
}

const FEliteAffix* EliteAffixFor(entt::registry& registry, int battleOrdinal) {
    if (!IsEliteBattle(registry, battleOrdinal)) return nullptr;
    const auto* sequence = FindRunSequence(registry);
    if (sequence == nullptr) return nullptr;
    return FindAffix(*sequence, EnsureRunPath(registry).affixId);
}

std::vector<std::string> GapEventPools(entt::registry& registry, int battleOrdinal) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* sequence = FindRunSequence(registry);
    if (run == nullptr || sequence == nullptr) return {};
    const auto* slot = sequence->GapEvent(sequence->PositionOf(run->depth - 1), battleOrdinal);
    if (slot == nullptr) return {};
    if (registry.ctx().find<FRunPathState>() != nullptr) {
        const FRunPathState& state = EnsureRunPath(registry);
        if (state.hasFork && !state.battle && state.choice == 1 && state.battleOrdinal == battleOrdinal) {
            return {slot->eventPool, std::string(kEventPoolMystery)};
        }
    }
    return {slot->eventPool};
}

FPathStrip BuildPathStrip(entt::registry& registry) {
    FPathStrip strip;
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* sequence = FindRunSequence(registry);
    if (run == nullptr || sequence == nullptr || registry.ctx().find<FRunPathState>() == nullptr) return strip;
    const auto position = sequence->PositionOf(run->depth - 1);
    const auto& rooms = sequence->DungeonAt(position).rooms;
    const FRunPathState state = EnsureRunPath(registry);

    for (const auto& room : rooms) {
        FPathNode node;
        if (room.type == "battle" && !room.boss.empty()) {
            node.kind = EPathNodeKind::Boss;
            node.label = "Boss";
            const auto* bosses = FindBossCatalog(registry);
            if (bosses != nullptr) {
                if (const auto* def = bosses->Resolve(sequence->themes[position.theme].id, room.boss)) {
                    node.label = "Boss: " + def->name;
                }
            }
        } else if (room.type == "battle") {
            node.kind = EPathNodeKind::Battle;
            node.label = "Battle";
        } else {
            node.kind = EPathNodeKind::Event;
            node.label = "Event";
        }
        strip.nodes.push_back(std::move(node));
    }

    if (state.hasFork && state.slot >= 0 && state.slot < static_cast<int>(strip.nodes.size())) {
        strip.forkNode = state.slot;
        FPathNode variant;
        if (state.battle) {
            variant.kind = EPathNodeKind::Elite;
            const auto* affix = FindAffix(*sequence, state.affixId);
            variant.label = "Elite: " + (affix != nullptr ? affix->displayName : state.affixId);
        } else {
            variant.kind = EPathNodeKind::Mystery;
            variant.label = "?";
        }
        auto& node = strip.nodes[static_cast<std::size_t>(state.slot)];
        if (state.choice == 1) {
            node = std::move(variant);
        } else if (state.choice == -1) {
            strip.forkAlt = std::move(variant);
        }
    }
    FPathNode camp;
    camp.kind = EPathNodeKind::Camp;
    camp.label = "Camp";
    strip.nodes.push_back(std::move(camp));

    if (const auto current = CurrentSlotIndex(registry)) {
        for (int index = 0; index < static_cast<int>(strip.nodes.size()); ++index) {
            auto& node = strip.nodes[static_cast<std::size_t>(index)];
            node.done = index < *current;
            node.current = index == *current;
            if (node.current && run->phase == EDungeonPhase::Reward) node.done = true;
        }
    }
    return strip;
}

std::vector<FForkOption> ForkOptions(entt::registry& registry) {
    if (registry.ctx().find<FRunPathState>() == nullptr || !IsForkPending(registry)) return {};
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* sequence = FindRunSequence(registry);
    const FRunPathState state = EnsureRunPath(registry);
    std::vector<FForkOption> options;
    if (state.battle) {
        const auto* affix = FindAffix(*sequence, state.affixId);
        if (affix == nullptr) return {};
        options.push_back({"Battle", "Standard rewards"});
        const std::string hpSentence =
            sequence->elite.healthPercent > 0
                ? "Enemy max HP +" + std::to_string(sequence->elite.healthPercent) + "%. "
                : std::string();
        options.push_back({"Elite: " + affix->displayName,
                           hpSentence + affix->text + ". " + EliteAffectedSkills(registry, *affix) +
                               "Reward: rune offer (uncommon or better)"});
        return options;
    }
    const auto& rooms = sequence->DungeonAt(sequence->PositionOf(run->depth - 1)).rooms;
    const std::vector<FEventDef> catalog = EventCatalog(registry);
    const auto id = PickEventId(catalog, rooms[static_cast<std::size_t>(state.slot)].eventPool,
                                EventSeen(registry), run->seed, run->depth, state.battleOrdinal);
    FForkOption named{"Event", "A known event"};
    if (id) {
        for (const auto& event : catalog) {
            if (event.id != *id) continue;
            named.title = "Event: " + event.title;
            named.detail.clear();
            for (const auto& choice : event.choices) {
                if (!named.detail.empty()) named.detail += " / ";
                named.detail += choice.text;
            }
            break;
        }
    }
    options.push_back(std::move(named));
    options.push_back({"? Unknown event",
                       "Hidden: any common or mystery event. Mystery events pay more; every event has a free choice"});
    return options;
}

} // namespace game
