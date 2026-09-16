#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game::gameplay {

/// What kind of room a dungeon slot holds (design §3: 몬스터 3 + 이벤트 1~2).
/// why an enum with one implemented member instead of a bare string: the *vocabulary* is the contract the
/// loader validates against — an unknown kind must fail the boot, not become a silently skipped room
/// (R19/R22). `Event` is deliberately declared but not implemented in this slice: the row can say it, the
/// run rejects it at load time until the event-room slice lands.
enum class ERoomKind {
    Monster,
    Event,
};

/// One dungeon as authored in `assets/data/dungeons.json`: an ordered list of rooms.
/// why rooms are encounter ids and not inline encounters: a room *is* a battle, and the battle already has
/// one authored shape (`encounters.json`). Two shapes for one thing would drift (R11).
/// `invariant:` `roomEncounterIds` and `roomKinds` are parallel arrays of the same length — the loader
/// rejects a mismatch, so a reader can index both with one loop variable.
struct FDungeonContent {
    std::string id;
    std::string displayName;
    std::vector<std::string> roomEncounterIds;
    std::vector<ERoomKind> roomKinds;

    [[nodiscard]] std::size_t roomCount() const { return roomEncounterIds.size(); }
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kDungeonField* indices below, decodeDungeon and assets/data/dungeons.json must
/// keep the same order and the same keys.
inline constexpr FContentField kDungeonFields[] = {
    {"id", EContentFieldType::Text, true},           // boundary: asset key
    {"display_name", EContentFieldType::Text, true}, // boundary: asset key
    {"rooms", EContentFieldType::IdArray, true},     // boundary: asset key — encounter ids, in run order
    {"room_kinds", EContentFieldType::IdArray, true},// boundary: asset key — parallel to `rooms`
};

inline constexpr std::size_t kDungeonFieldId = 0;
inline constexpr std::size_t kDungeonFieldDisplayName = 1;
inline constexpr std::size_t kDungeonFieldRooms = 2;
inline constexpr std::size_t kDungeonFieldRoomKinds = 3;

static_assert(std::size(kDungeonFields) == 4, "kDungeonFields and the kDungeonField* indices drifted apart");

/// Room-kind token → enum, or nullopt for a token the vocabulary does not know.
/// why a function here instead of a switch inside the registry: the token is part of this type's asset
/// vocabulary, so its parse belongs next to the type (R12: one place answers "what can a dungeon row say?").
[[nodiscard]] std::optional<ERoomKind> roomKindFromText(std::string_view text);

/// Projects one validated row into a dungeon. Cannot fail on types (R22); the parallel-array length check and
/// the encounter-reference check belong to FContentRegistry, which owns every table at once.
[[nodiscard]] FDungeonContent decodeDungeon(const FContentRow& row);

} // namespace game::gameplay
