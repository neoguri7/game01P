#include "gameplay/run/FPrototypeRun.h"

#include <algorithm>
#include <cmath>
#include <set>

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

        const auto passiveTreeDocument = FJsonLoader::Load(document->at("passive_tree").get<std::string>());
        if (!passiveTreeDocument) return std::nullopt;
        std::set<std::string> passiveNodeIds;
        for (const auto& row : passiveTreeDocument->at("nodes")) {
            FPassiveNode node;
            node.id = row.at("id").get<std::string>();
            node.displayName = row.at("display_name").get<std::string>();
            node.description = row.at("description").get<std::string>();
            if (!row.at("parent").is_null()) node.parentId = row.at("parent").get<std::string>();
            node.x = row.at("x").get<float>();
            node.y = row.at("y").get<float>();
            const auto& bonuses = row.at("bonuses");
            node.bonuses.maxHealth = bonuses.at("max_health").get<int>();
            node.bonuses.attack = bonuses.at("attack").get<int>();
            node.bonuses.magicAttack = bonuses.at("magic_attack").get<int>();
            node.bonuses.defense = bonuses.at("defense").get<int>();
            node.bonuses.magicDefense = bonuses.at("magic_defense").get<int>();
            node.bonuses.speed = bonuses.at("speed").get<int>();
            const bool validBonuses = node.bonuses.maxHealth >= 0 && node.bonuses.attack >= 0
                && node.bonuses.magicAttack >= 0 && node.bonuses.defense >= 0
                && node.bonuses.magicDefense >= 0 && node.bonuses.speed >= 0;
            if (node.id.empty() || node.displayName.empty() || node.description.empty()
                || !passiveNodeIds.insert(node.id).second || !std::isfinite(node.x) || !std::isfinite(node.y)
                || node.x < 0.0f || node.x > 100.0f || node.y < 0.0f || node.y > 100.0f || !validBonuses) {
                return std::nullopt;
            }
            if (node.parentId.empty()) {
                if (!run.passiveRootNodeId.empty() || node.bonuses.maxHealth != 0 || node.bonuses.attack != 0
                    || node.bonuses.magicAttack != 0 || node.bonuses.defense != 0
                    || node.bonuses.magicDefense != 0 || node.bonuses.speed != 0) return std::nullopt;
                run.passiveRootNodeId = node.id;
            }
            run.passiveNodes.push_back(std::move(node));
        }
        if (run.passiveNodes.size() < 2 || run.passiveRootNodeId.empty()) return std::nullopt;
        for (const FPassiveNode& node : run.passiveNodes) {
            std::set<std::string> visited{node.id};
            std::string parentId = node.parentId;
            while (!parentId.empty()) {
                if (!visited.insert(parentId).second) return std::nullopt;
                const auto parent = std::ranges::find(run.passiveNodes, parentId, &FPassiveNode::id);
                if (parent == run.passiveNodes.end()) return std::nullopt;
                parentId = parent->parentId;
            }
        }

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
    return members > 0 && members <= 4 && !rooms.empty() && !passiveNodes.empty();
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
    if (member == party.end() || !member->enabled || member->passivePoints <= 0
        || std::ranges::find(member->allocatedNodes, nodeId) != member->allocatedNodes.end()) return false;
    const auto node = std::ranges::find(passiveNodes, nodeId, &FPassiveNode::id);
    if (node == passiveNodes.end() || node->id == passiveRootNodeId) return false;
    return node->parentId == passiveRootNodeId
        || std::ranges::find(member->allocatedNodes, node->parentId) != member->allocatedNodes.end();
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
    FPassiveStatBonuses total;
    const auto member = std::ranges::find(party, placementId, &FPreparedMember::placementId);
    if (member == party.end()) return total;
    for (const std::string& nodeId : member->allocatedNodes) {
        const auto node = std::ranges::find(passiveNodes, nodeId, &FPassiveNode::id);
        if (node == passiveNodes.end()) continue;
        total.maxHealth += node->bonuses.maxHealth;
        total.attack += node->bonuses.attack;
        total.magicAttack += node->bonuses.magicAttack;
        total.defense += node->bonuses.defense;
        total.magicDefense += node->bonuses.magicDefense;
        total.speed += node->bonuses.speed;
    }
    return total;
}

const FPrototypeRoom* FPrototypeRun::CurrentRoom() const {
    return active && roomIndex < rooms.size() ? &rooms[roomIndex] : nullptr;
}

} // namespace game
