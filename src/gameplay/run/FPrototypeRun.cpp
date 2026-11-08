#include "gameplay/run/FPrototypeRun.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "core/JsonLoader.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/factory/FEntityFactory.h"

namespace game {

std::optional<FPrototypeRun> FPrototypeRun::Load(const gameplay::FContentRegistry& content) {
    const auto document = FJsonLoader::Load("assets/data/prototype-run.json");
    if (!document) return std::nullopt;
    FPrototypeRun run;
    try {
        run.skillSlots = document->at("skill_slots").get<int>();
        if (run.skillSlots <= 0 || run.skillSlots > 4) return std::nullopt;
        run.experiencePerLevelBase = document->at("experience_per_level").at("base").get<int>();
        run.experiencePerLevelStep = document->at("experience_per_level").at("step").get<int>();
        if (run.experiencePerLevelBase <= 0 || run.experiencePerLevelStep < 0) return std::nullopt;
        for (const auto& runeIdValue : document->at("starter_runes")) {
            const std::string runeId = runeIdValue.get<std::string>();
            if (content.runes.find(runeId) == nullptr) return std::nullopt;
            run.runeInventory.push_back(runeId);
        }

        // WP-32h: schema version is validated by FPassiveTree::Load alone (v1 and v2), not by FJsonLoader.
        const auto tree = LoadPassiveTreeFile(document->at("passive_tree").get<std::string>());
        if (!tree) return std::nullopt;
        run.tree = *tree;

        int battles = 0;
        int events = 0;
        for (const auto& row : document->at("rooms")) {
            FPrototypeRoom room;
            room.title = row.at("title").get<std::string>();
            const auto kind = row.at("kind").get<std::string>();
            if (room.title.empty()) return std::nullopt;
            if (kind == "battle") {
                room.kind = EPrototypeRoom::Battle;
                room.battlefield = row.at("battlefield").get<std::string>();
                room.experienceReward = row.at("experience_reward").get<int>();
                if (room.experienceReward <= 0) return std::nullopt;
                const auto battlefieldDocument = FJsonLoader::Load(room.battlefield);
                if (!battlefieldDocument) return std::nullopt;
                entt::registry registry;
                FScene scene(registry, "RunPreflight");
                scene.Load();
                const auto battlefield = FEntityFactory::CreateBattlefield(scene, *battlefieldDocument);
                if (!battlefield || !SetupCombatUnits(registry, *battlefield, content)) return std::nullopt;
                if (battles == 0) {
                    for (const auto& unit : battlefield->units) {
                        if (unit.faction != EUnitFaction::Party) continue;
                        FPreparedMember member{unit.id, unit.unitTemplateId};
                        const auto* definition = content.units.find(member.unitId);
                        if (!definition) return std::nullopt;
                        for (const auto& skill : definition->skillIds) {
                            if (member.skills.size() == static_cast<std::size_t>(run.skillSlots)) break;
                            member.skills.push_back(FSkillSlot{skill, {}});
                        }
                        run.party.push_back(std::move(member));
                    }
                } else {
                    for (const auto& member : run.party) {
                        if (!std::ranges::any_of(battlefield->units, [&](const FUnitPlacement& unit) {
                            return unit.id == member.placementId && unit.unitTemplateId == member.unitId
                                && unit.faction == EUnitFaction::Party;
                        })) return std::nullopt;
                    }
                }
                ++battles;
            } else if (kind == "event") {
                room.kind = EPrototypeRoom::Event;
                ++events;
            } else if (kind == "camp") {
                room.kind = EPrototypeRoom::Camp;
            } else {
                return std::nullopt;
            }
            run.rooms.push_back(std::move(room));
        }
        if (run.rooms.empty() || battles != 3 || events < 1 || events > 2
            || run.rooms.front().kind != EPrototypeRoom::Battle
            || run.rooms.back().kind != EPrototypeRoom::Camp
            || std::ranges::count_if(run.rooms, [](const FPrototypeRoom& room) {
                return room.kind == EPrototypeRoom::Camp;
            }) != 1 || run.party.empty() || run.party.size() > 4) return std::nullopt;
    } catch (const nlohmann::json::exception& error) {
        LOG_ERROR("Prototype run definition failed: {}", error.what());
        return std::nullopt;
    }
    return run;
}

std::optional<FPassiveTree> FPrototypeRun::LoadPassiveTreeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("prototype passive tree file is missing or unreadable: {}", path.string());
        return std::nullopt;
    }
    auto document = nlohmann::json::parse(file, nullptr, false);
    if (document.is_discarded()) {
        LOG_ERROR("prototype passive tree file is not valid JSON: {}", path.string());
        return std::nullopt;
    }
    return FPassiveTree::Load(document);
}

bool FPrototypeRun::CanStart(const gameplay::FContentRegistry& content) const {
    std::size_t members = 0;
    for (const auto& member : party) {
        if (!member.enabled) continue;
        ++members;
        const auto* unit = content.units.find(member.unitId);
        if (!unit || member.skills.empty() || member.skills.size() > static_cast<std::size_t>(skillSlots)) return false;
        std::set<std::string> selected;
        for (const auto& slot : member.skills) {
            if (std::ranges::find(unit->skillIds, slot.skillId) == unit->skillIds.end()
                || !selected.insert(slot.skillId).second || ValidateSkillSlot(slot, content)) return false;
        }
    }
    return members > 0 && members <= 4 && !rooms.empty() && !tree.empty();
}

bool FPrototypeRun::Start(const gameplay::FContentRegistry& content) {
    if (active || !CanStart(content)) return false;
    for (auto& member : party) {
        member.experience = 0;
        member.level = 1;
        member.passivePoints = 0;
        member.allocatedNodes.clear();
    }
    active = true;
    roomIndex = 0;
    dungeonIndex = completedDungeons;
    return true;
}

void FPrototypeRun::End() {
    active = false;
    roomIndex = 0;
    for (auto& member : party) {
        for (auto& slot : member.skills) {
            runeInventory.insert(runeInventory.end(), slot.runeIds.begin(), slot.runeIds.end());
            slot.runeIds.clear();
        }
        member.experience = 0;
        member.level = 1;
        member.passivePoints = 0;
        member.allocatedNodes.clear();
    }
}

bool FPrototypeRun::CanEditLoadout() const {
    if (!active) return true;
    const FPrototypeRoom* room = CurrentRoom();
    return room != nullptr && room->kind == EPrototypeRoom::Camp;
}

bool FPrototypeRun::EquipRune(
    std::string_view placementId, std::string_view skillId, std::string_view runeId,
    const gameplay::FContentRegistry& content) {
    if (!CanEditLoadout() || content.runes.find(runeId) == nullptr) return false;
    const auto member = std::ranges::find(party, placementId, &FPreparedMember::placementId);
    if (member == party.end() || !member->enabled) return false;
    const auto slot = std::ranges::find(member->skills, skillId, &FSkillSlot::skillId);
    const auto available = std::ranges::find(runeInventory, runeId);
    if (slot == member->skills.end() || available == runeInventory.end()) return false;
    FSkillSlot candidate = *slot;
    candidate.runeIds.emplace_back(runeId);
    if (ValidateSkillSlot(candidate, content)) return false;
    slot->runeIds.emplace_back(runeId);
    runeInventory.erase(available);
    return true;
}

bool FPrototypeRun::UnequipRune(
    std::string_view placementId, std::string_view skillId, std::string_view runeId) {
    if (!CanEditLoadout()) return false;
    const auto member = std::ranges::find(party, placementId, &FPreparedMember::placementId);
    if (member == party.end()) return false;
    const auto slot = std::ranges::find(member->skills, skillId, &FSkillSlot::skillId);
    if (slot == member->skills.end()) return false;
    const auto equipped = std::ranges::find(slot->runeIds, runeId);
    if (equipped == slot->runeIds.end()) return false;
    runeInventory.emplace_back(*equipped);
    slot->runeIds.erase(equipped);
    return true;
}

bool FPrototypeRun::UnequipSkill(std::string_view placementId, std::string_view skillId) {
    if (!CanEditLoadout()) return false;
    const auto member = std::ranges::find(party, placementId, &FPreparedMember::placementId);
    if (member == party.end()) return false;
    const auto slot = std::ranges::find(member->skills, skillId, &FSkillSlot::skillId);
    if (slot == member->skills.end()) return false;
    runeInventory.insert(runeInventory.end(), slot->runeIds.begin(), slot->runeIds.end());
    member->skills.erase(slot);
    return true;
}

void FPrototypeRun::Advance() {
    if (!active || rooms.empty()) return;
    ++roomIndex;
    if (roomIndex == rooms.size()) {
        roomIndex = 0;
        ++dungeonIndex;
    }
    if (rooms[roomIndex].kind == EPrototypeRoom::Camp) {
        completedDungeons = std::max(completedDungeons, dungeonIndex + 1);
    }
}

bool FPrototypeRun::ResolveCurrentBattle(bool victory) {
    const FPrototypeRoom* room = CurrentRoom();
    if (!active || room == nullptr || room->kind != EPrototypeRoom::Battle) return false;
    if (!victory) {
        End();
        return true;
    }

    for (FPreparedMember& member : party) {
        if (!member.enabled) continue;
        member.experience += room->experienceReward;
        while (member.experience >= ExperienceForNextLevel(member)) {
            member.experience -= ExperienceForNextLevel(member);
            ++member.level;
            ++member.passivePoints;
        }
    }
    Advance();
    return true;
}

bool FPrototypeRun::CanAllocatePassiveNode(std::string_view placementId, std::string_view nodeId) const {
    if (!active) return false;
    const FPrototypeRoom* room = CurrentRoom();
    if (room != nullptr && room->kind == EPrototypeRoom::Battle) return false;
    const auto member = std::ranges::find(party, placementId, &FPreparedMember::placementId);
    if (member == party.end() || !member->enabled || member->passivePoints <= 0) return false;
    return tree.CanAllocate(member->allocatedNodes, nodeId);
}

bool FPrototypeRun::AllocatePassiveNode(std::string_view placementId, std::string_view nodeId) {
    if (!CanAllocatePassiveNode(placementId, nodeId)) return false;
    const auto member = std::ranges::find(party, placementId, &FPreparedMember::placementId);
    member->allocatedNodes.emplace_back(nodeId);
    --member->passivePoints;
    return true;
}

int FPrototypeRun::ExperienceForNextLevel(const FPreparedMember& member) const {
    return experiencePerLevelBase + experiencePerLevelStep * (member.level - 1);
}

FPassiveStatBonuses FPrototypeRun::PassiveBonuses(std::string_view placementId) const {
    const auto member = std::ranges::find(party, placementId, &FPreparedMember::placementId);
    if (member == party.end()) return FPassiveStatBonuses{};
    return tree.Bonuses(member->allocatedNodes);
}

const FPrototypeRoom* FPrototypeRun::CurrentRoom() const {
    return active && roomIndex < rooms.size() ? &rooms[roomIndex] : nullptr;
}

} // namespace game
