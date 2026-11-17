#include "gameplay/dungeon/FRunBriefing.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FEncounterContent.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunSequence.h"

namespace game {
namespace {

constexpr const char* kMiddleDot = "\xC2\xB7";  // U+00B7, as FPreparationView.cpp

[[nodiscard]] std::string PluralOf(const std::string& name, int count)
{
    if (count == 1) return name;
    if (!name.empty() && name.back() == 's') return name + "es";
    return name + "s";
}

[[nodiscard]] int SafeBalance(entt::registry& registry, const char* key)
{
    try {
        return DungeonBalance(registry, key);
    } catch (const std::exception&) {
        LOG_WARN("The briefing could not read balance '{}'.", key);
        return 0;
    }
}

/// "Final boss" if any room has boss "final", else "Mid boss" if "mid", else "".
[[nodiscard]] std::string BossMarkOf(const FRunDungeonDef& dungeon)
{
    bool mid = false;
    for (const FRunRoomSlot& room : dungeon.rooms) {
        if (room.boss == kBossSlotFinal) return "Final boss";
        if (room.boss == kBossSlotMid) mid = true;
    }
    return mid ? "Mid boss" : "";
}

/// The first battle slot that names a boss, nullptr when the dungeon has none.
[[nodiscard]] const FRunRoomSlot* BossSlotOf(const FRunDungeonDef& dungeon)
{
    for (const FRunRoomSlot& room : dungeon.rooms) {
        if (room.type == "battle" && !room.boss.empty()) return &room;
    }
    return nullptr;
}

[[nodiscard]] std::string GateLineOf(const FBossDef& def)
{
    for (const FBossPhaseDef& phase : def.phases) {
        if (!phase.partsGate) continue;
        int count = 0;
        for (const FBossPartDef& part : def.parts) count += part.count;
        if (count <= 0 || def.parts.empty()) return {};
        return "Cannot drop below " + std::to_string(phase.hpAbove) + "% health while its " + std::to_string(count) +
               " " + PluralOf(def.parts.front().name, count) + " stand.";
    }
    return {};
}

[[nodiscard]] std::string StakesOf(entt::registry& registry, const FRunSequence& sequence)
{
    std::string line = "Elite: enemies +" + std::to_string(sequence.elite.healthPercent) + "% max HP";
    const auto* rewards = registry.ctx().find<FRewards>();
    const FRarityWeights* table = rewards != nullptr ? FindRarityTable(rewards->tables, kRarityTableBoss) : nullptr;
    if (table != nullptr) {
        if (table->common == 0 && table->uncommon > 0) return line + "; a win offers a rune of Uncommon or better.";
        if (table->common == 0 && table->uncommon == 0 && table->rare > 0) return line + "; a win offers a rune of Rare.";
    }
    return line + ".";
}

} // namespace

EBriefingNodeKind BriefingKindOf(EPathNodeKind kind)
{
    switch (kind) {
    case EPathNodeKind::Battle: return EBriefingNodeKind::Battle;
    case EPathNodeKind::Elite: return EBriefingNodeKind::Elite;
    case EPathNodeKind::Event: return EBriefingNodeKind::Event;
    case EPathNodeKind::Mystery: return EBriefingNodeKind::Mystery;
    case EPathNodeKind::Boss: return EBriefingNodeKind::Boss;
    case EPathNodeKind::Camp: return EBriefingNodeKind::Camp;
    }
    return EBriefingNodeKind::Battle;
}

std::optional<FRunBriefing> BuildRunBriefing(entt::registry& registry)
{
    const auto* runPtr = registry.ctx().find<FDungeonRun>();
    const auto* sequence = FindRunSequence(registry);
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (runPtr == nullptr || sequence == nullptr || content == nullptr) return std::nullopt;
    const FDungeonRun& run = *runPtr;
    const FRunSequence::FPosition position = sequence->PositionOf(run.depth - 1);
    if (position.theme >= sequence->themes.size()) return std::nullopt;
    const FRunThemeDef& theme = sequence->themes[position.theme];
    if (position.dungeonInTheme >= theme.dungeons.size()) return std::nullopt;
    const FRunDungeonDef& dungeon = sequence->DungeonAt(position);

    FRunBriefing out;
    out.themeId = theme.id;
    out.themeName = theme.name;
    out.dungeonName = dungeon.name;
    out.dungeonNumber = static_cast<int>(position.dungeonInTheme) + 1;
    out.dungeonCount = static_cast<int>(theme.dungeons.size());
    out.depth = run.depth;
    out.cycle = position.cycle;
    out.infoLine = "Dungeon " + std::to_string(out.dungeonNumber) + " of " + std::to_string(out.dungeonCount) + "  " +
                   kMiddleDot + "  Depth " + std::to_string(out.depth);
    if (out.cycle >= 1) out.infoLine += std::string("  ") + kMiddleDot + "  Cycle " + std::to_string(out.cycle);
    out.enterLabel = "Enter " + dungeon.name;

    // Boss slot and objective.
    const FRunRoomSlot* bossSlot = BossSlotOf(dungeon);
    const FBossCatalog* bosses = FindBossCatalog(registry);
    const FBossDef* bossDef =
        (bossSlot != nullptr && bosses != nullptr) ? bosses->Resolve(theme.id, bossSlot->boss) : nullptr;
    if (bossSlot == nullptr) {
        out.objective = "Objective: Clear every room.";
    } else if (bossDef == nullptr) {
        out.objective = "Objective: Defeat the boss.";
    } else if (bossSlot->boss == kBossSlotFinal) {
        out.objective = "Objective: Defeat " + bossDef->name + " to conquer " + theme.name + ".";
    } else {
        out.objective = "Objective: Defeat " + bossDef->name + ".";
    }

    // Danger.
    out.danger = "Danger: depth " + std::to_string(out.depth) + ". Enemies grow stronger with each dungeon and each room.";
    if (out.cycle >= 1) {
        const FCycleScale scale = CycleScaleFor(registry);
        if (scale.healthPct != 0 || scale.attackPct != 0) {
            out.danger += " Cycle " + std::to_string(out.cycle) + ": enemies +" + std::to_string(scale.healthPct) +
                          "% health, +" + std::to_string(scale.attackPct) + "% attack.";
        }
    }

    // Route (EnsureRunPath inside BuildPathStrip may derive the fork).
    const FPathStrip strip = BuildPathStrip(registry);
    out.route.push_back({EBriefingNodeKind::Entrance, "Entrance", std::nullopt, {}});
    for (std::size_t i = 0; i < strip.nodes.size(); ++i) {
        FBriefingNode node{BriefingKindOf(strip.nodes[i].kind), strip.nodes[i].label, std::nullopt, {}};
        if (static_cast<int>(i) == strip.forkNode && strip.forkAlt.has_value()) {
            node.altKind = BriefingKindOf(strip.forkAlt->kind);
            node.altLabel = strip.forkAlt->label;
        }
        out.route.push_back(std::move(node));
    }

    // Elite option.
    const FRunPathState& path = EnsureRunPath(registry);
    if (path.hasFork && path.battle && path.choice == -1) {
        const auto affix = std::ranges::find(sequence->elite.affixes, path.affixId, &FEliteAffix::id);
        if (affix != sequence->elite.affixes.end()) {
            out.eliteLine = "Elite option (" + affix->displayName + "): " + affix->text + ".";
            out.eliteStakes = StakesOf(registry, *sequence);
        }
    }

    // Expedition row.
    for (std::size_t i = 0; i < theme.dungeons.size(); ++i) {
        FBriefingDungeon entry;
        entry.name = theme.dungeons[i].name;
        entry.bossMark = BossMarkOf(theme.dungeons[i]);
        entry.cleared = i < position.dungeonInTheme;
        entry.current = i == position.dungeonInTheme;
        out.expedition.push_back(std::move(entry));
    }

    // Boss block.
    if (bossDef != nullptr) {
        FBriefingBoss boss;
        boss.name = bossDef->name;
        if (const auto* body = content->units.find(bossDef->unit)) boss.sprite = body->spriteId;
        boss.mark = bossSlot->boss == kBossSlotFinal ? "FINAL BOSS" : "MID BOSS";
        if (!bossDef->phases.empty()) boss.note = bossDef->phases.front().notes;
        boss.gateLine = GateLineOf(*bossDef);
        out.boss = std::move(boss);
    }

    // Threats: the kinds the dungeon can field, plus the boss room's escort pool.
    std::vector<std::string> threatIds;
    if (const auto* encounters = FindEncounterContent(registry)) {
        threatIds = DungeonThreatUnits(*encounters, dungeon, theme.id, run.depth);
    }
    if (bossSlot != nullptr) {
        const nlohmann::json& generation = DungeonDefinition(registry);
        const auto gen = generation.find("generation");
        if (gen != generation.end() && gen->is_object()) {
            const auto enemies = gen->find("enemies");
            if (enemies != gen->end() && enemies->is_array()) {
                for (const nlohmann::json& row : *enemies) {
                    const auto unit = row.find("unit");
                    if (unit == row.end() || !unit->is_string()) continue;
                    const std::string id = unit->get<std::string>();
                    if (std::ranges::find(threatIds, id) == threatIds.end()) threatIds.push_back(id);
                }
            }
        }
    }
    for (const std::string& id : threatIds) {
        FBriefingUnit unit{id, id, {}};
        if (const auto* row = content->units.find(id)) {
            unit.name = row->displayName;
            unit.sprite = row->spriteId;
        }
        out.threats.push_back(std::move(unit));
    }

    // Party.
    for (const FRunMember& member : run.party) {
        FBriefingMember entry;
        const auto* unit = ResolveRunMemberUnit(registry, member.id);
        entry.name = member.displayName;
        if (unit != nullptr) {
            entry.role = unit->displayName;
            entry.sprite = unit->spriteId;
            if (entry.name.empty()) entry.name = unit->displayName;
        }
        // "Alden / Warrior": the name is the part before " / ", the role line the part after it.
        if (const auto split = member.displayName.find(" / "); split != std::string::npos && split > 0) {
            entry.name = member.displayName.substr(0, split);
            if (split + 3 < member.displayName.size()) entry.role = member.displayName.substr(split + 3);
        }
        entry.health = DungeonEffectiveHealth(registry, member);
        entry.maxHealth = DungeonMemberMaxHealth(registry, member);
        entry.level = member.level;
        out.party.push_back(std::move(entry));
    }

    // Reward line.
    out.rewardLine = "Each battle won: +" + std::to_string(SafeBalance(registry, "room_experience")) +
                     " EXP per member, +" + std::to_string(SafeBalance(registry, "room_materials")) + " materials";
    const auto* rewards = registry.ctx().find<FRewards>();
    if (rewards != nullptr) {
        out.rewardLine += ", " + std::to_string(std::lround(rewards->tables.chance * 100.0)) + "% chance of a rune offer";
    }
    out.rewardLine += ".";
    if (bossSlot != nullptr && rewards != nullptr) {
        if (rewards->tables.bossOffer == kRewardBossOfferGuaranteed) out.rewardLine += " The boss always offers a rune.";
        else if (rewards->tables.bossOffer == kRewardBossOfferExtra) out.rewardLine += " The boss offers a rune, then one more.";
    }
    return out;
}

} // namespace game
