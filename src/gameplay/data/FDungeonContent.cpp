#include "gameplay/data/FDungeonContent.h"

#include <string>

namespace game::gameplay {

std::optional<ERoomKind> roomKindFromText(std::string_view text) {
    // why the token table lives here and not in the registry: a token is part of this type's asset
    // vocabulary, so "what can a dungeon row say?" has one answer (R12). A miss is nullopt rather than a
    // default because decode cannot fail (R22) — the registry turns it into a boot failure (R19).
    if (text == "monster") {
        return ERoomKind::Monster;
    }
    if (text == "event") {
        return ERoomKind::Event;
    }
    return std::nullopt;
}

FDungeonContent decodeDungeon(const FContentRow& row) {
    FDungeonContent dungeon;
    dungeon.id = row.text(kDungeonFieldId);
    dungeon.displayName = row.text(kDungeonFieldDisplayName);
    dungeon.roomEncounterIds = row.textArray(kDungeonFieldRooms);

    // invariant: roomKinds stays parallel to roomEncounterIds; FContentRegistry rejects a length mismatch, so
    // this loop can index both with one variable. An unknown kind token is a boot failure there, which is why
    // the projection falls back to Monster instead of leaving a hole in the parallel array.
    const std::vector<std::string> kindTokens = row.textArray(kDungeonFieldRoomKinds);
    dungeon.roomKinds.reserve(kindTokens.size());
    for (const std::string& token : kindTokens) {
        // fallback: Monster — a safe value the registry overwrites with a boot failure on any unknown token.
        dungeon.roomKinds.push_back(roomKindFromText(token).value_or(ERoomKind::Monster));
    }
    return dungeon;
}

} // namespace game::gameplay
