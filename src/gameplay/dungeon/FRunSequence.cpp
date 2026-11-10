#include "gameplay/dungeon/FRunSequence.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRoomGenerator.h"

namespace game {
namespace {

/// The static shape rules of a v2 room sequence (the dynamic boss/family/room-count consumers read the
/// parsed struct, never the document). Each rule fails the boot with its own message.
std::optional<FRunSequence> Fail(std::string& error, std::string message) {
    error = std::move(message);
    return std::nullopt;
}

constexpr int kMaxCyclePercent = 1000;  // percents are integers; anything above is authored garbage
constexpr int kMaxStartSupportValue = 100000;  // materials/offer counts; the same "reject garbage" ceiling

bool IsKnownSlotType(std::string_view type) {
    return type == "battle" || type == "event";
}

} // namespace

std::optional<FRunSequence> FRunSequence::Load(const nlohmann::json& document, std::string& error) {
    if (!document.is_object()) return Fail(error, "the dungeon document must be an object");
    const auto themes = document.find("themes");
    if (themes == document.end() || !themes->is_array() || themes->empty()) {
        return Fail(error, "`themes` must be a nonempty array");
    }

    FRunSequence sequence;
    // D-23 §2: heavy families are reserved for elite/boss rooms, and the boss-room question stays a data
    // flip until WP-14 (ship false). Parsed before the themes so slot validation can consult it.
    if (const auto rules = document.find("family_rules"); rules != document.end()) {
        if (!rules->is_object()) return Fail(error, "`family_rules` must be an object");
        if (const auto allow = rules->find("boss_rooms_allow_heavy"); allow != rules->end()) {
            if (!allow->is_boolean()) {
                return Fail(error, "family_rules `boss_rooms_allow_heavy` must be a boolean");
            }
            sequence.bossRoomsAllowHeavy = allow->get<bool>();
        }
    }
    std::vector<std::string> themeIds;
    for (const auto& themeRow : *themes) {
        if (!themeRow.is_object()) return Fail(error, "every theme must be an object");
        const auto id = themeRow.find("id");
        const auto name = themeRow.find("name");
        if (id == themeRow.end() || !id->is_string() || id->get_ref<const std::string&>().empty() ||
            std::ranges::find(themeIds, id->get_ref<const std::string&>()) != themeIds.end()) {
            return Fail(error, "every theme needs a unique nonempty `id`");
        }
        if (name == themeRow.end() || !name->is_string() || name->get_ref<const std::string&>().empty()) {
            return Fail(error, "theme '" + id->get_ref<const std::string&>() + "' needs a nonempty `name`");
        }
        FRunThemeDef theme;
        theme.id = id->get<std::string>();
        theme.name = name->get<std::string>();
        themeIds.push_back(theme.id);

        const auto dungeons = themeRow.find("dungeons");
        if (dungeons == themeRow.end() || !dungeons->is_array() || dungeons->empty()) {
            return Fail(error, "theme '" + theme.id + "' needs a nonempty `dungeons` array");
        }
        for (const auto& dungeonRow : *dungeons) {
            if (!dungeonRow.is_object()) return Fail(error, "every dungeon must be an object");
            const auto dungeonName = dungeonRow.find("name");
            if (dungeonName == dungeonRow.end() || !dungeonName->is_string() ||
                dungeonName->get_ref<const std::string&>().empty()) {
                return Fail(error, "every dungeon needs a nonempty `name`");
            }
            FRunDungeonDef dungeon;
            dungeon.name = dungeonName->get<std::string>();
            int slotNumber = 0;  // 1-based in the authored order; named in the strength refusals

            const auto rooms = dungeonRow.find("rooms");
            if (rooms == dungeonRow.end() || !rooms->is_array() || rooms->empty() ||
                static_cast<int>(rooms->size()) > kMaxDungeonSlots) {
                return Fail(error, "dungeon '" + dungeon.name + "' needs 1.." +
                                       std::to_string(kMaxDungeonSlots) + " room slots");
            }
            for (const auto& roomRow : *rooms) {
                if (!roomRow.is_object()) return Fail(error, "every room slot must be an object");
                FRunRoomSlot slot;
                ++slotNumber;
                const auto type = roomRow.find("type");
                if (type == roomRow.end() || !type->is_string() ||
                    !IsKnownSlotType(type->get_ref<const std::string&>())) {
                    return Fail(error, "room slot `type` must be 'battle' or 'event'");
                }
                slot.type = type->get<std::string>();
                // WP-08b: the family set is generator code and a missing one means "open"; an unknown name is
                // a boot refusal. The strength rule runs after `boss` is read (it needs it).
                slot.family = "open";
                if (const auto family = roomRow.find("family"); family != roomRow.end()) {
                    if (!family->is_string()) return Fail(error, "room slot `family` must be a string");
                    const auto& familyId = family->get_ref<const std::string&>();
                    if (!familyId.empty()) {
                        if (!gameplay::FamilyStrengthOf(familyId)) {
                            return Fail(error, "room family '" + familyId +
                                                   "' is unknown (open, open_variant, corridor, cover, split)");
                        }
                        slot.family = familyId;
                    }
                }
                if (const auto boss = roomRow.find("boss"); boss != roomRow.end()) {
                    if (!boss->is_string()) return Fail(error, "room slot `boss` must be a string");
                    const auto& bossId = boss->get_ref<const std::string&>();
                    if (!bossId.empty() && bossId != "mid" && bossId != "final") {
                        return Fail(error, "room slot `boss` must be 'mid', 'final' or empty");
                    }
                    if (!bossId.empty() && slot.type != "battle") {
                        return Fail(error, "`boss` is only valid on a battle slot");
                    }
                    slot.boss = bossId;
                }
                for (const auto& [key, target] :
                     {std::pair<const char*, std::string*>{"encounter", &slot.encounter},
                      std::pair<const char*, std::string*>{"event_pool", &slot.eventPool}}) {
                    const auto found = roomRow.find(key);
                    if (found == roomRow.end()) continue;
                    if (!found->is_string()) {
                        return Fail(error, std::string("room slot `") + key + "` must be a string");
                    }
                    *target = found->get<std::string>();
                }
                // D-23 strength enforcement (a code rule with a data flip): normal monster rooms are light;
                // a heavy family needs a boss slot AND `family_rules.boss_rooms_allow_heavy` (ship false
                // until WP-14). The refusal names the slot and the rule. Event slots generate no room, so
                // the rule only binds battle slots.
                if (slot.type == "battle") {
                    const auto strength = gameplay::FamilyStrengthOf(slot.family);
                    const std::string where = "dungeon '" + dungeon.name + "' slot " + std::to_string(slotNumber);
                    if (strength == gameplay::EFamilyStrength::Heavy) {
                        if (slot.boss.empty()) {
                            return Fail(error, where + ": '" + slot.family +
                                                   "' is a heavy room family; normal monster rooms stay light (D-23)");
                        }
                        if (!sequence.bossRoomsAllowHeavy) {
                            return Fail(error, where + " (boss '" + slot.boss + "'): '" + slot.family +
                                                   "' is a heavy room family; boss rooms allow heavy only when "
                                                   "family_rules.boss_rooms_allow_heavy is true (D-23 §2 ships false)");
                        }
                    }
                }
                dungeon.rooms.push_back(std::move(slot));
            }

            // Gap rules (확정 §3: "battle 3 + event 1-2"): battles open and close the dungeon, and a
            // non-battle slot only exists inside a battle-to-battle gap — at most one per gap.
            if (dungeon.rooms.front().type != "battle" || dungeon.rooms.back().type != "battle") {
                return Fail(error, "dungeon '" + dungeon.name + "': the first and last slot must be a battle");
            }
            for (std::size_t index = 1; index < dungeon.rooms.size(); ++index) {
                if (dungeon.rooms[index - 1].type != "battle" && dungeon.rooms[index].type != "battle") {
                    return Fail(error, "dungeon '" + dungeon.name +
                                           "': non-battle slots need a battle between them (at most one per gap)");
                }
            }
            theme.dungeons.push_back(std::move(dungeon));
        }
        sequence.themes.push_back(std::move(theme));
    }

    if (const auto cycles = document.find("cycles"); cycles != document.end()) {
        if (!cycles->is_array()) return Fail(error, "`cycles` must be an array");
        for (const auto& row : *cycles) {
            if (!row.is_object()) return Fail(error, "every cycle row must be an object");
            FRunCycleStep step;
            for (const auto& [key, target] :
                 {std::pair<const char*, int*>{"health_pct", &step.healthPct},
                  std::pair<const char*, int*>{"attack_pct", &step.attackPct}}) {
                const auto found = row.find(key);
                if (found == row.end() || !found->is_number_integer()) {
                    return Fail(error, std::string("cycle row `") + key + "` must be an integer");
                }
                const auto value = found->get<std::int64_t>();
                if (value < 0 || value > kMaxCyclePercent) {
                    return Fail(error, std::string("cycle row `") + key + "` is out of range (0.." +
                                           std::to_string(kMaxCyclePercent) + ")");
                }
                *target = static_cast<int>(value);
            }
            sequence.cycles.push_back(step);
        }
    }

    if (const auto start = document.find("start_support"); start != document.end()) {
        if (!start->is_object()) return Fail(error, "`start_support` must be an object");
        for (const auto& [key, target] :
             {std::pair<const char*, int*>{"materials_per_dungeon", &sequence.startSupport.materialsPerDungeon},
              std::pair<const char*, int*>{"materials_cap", &sequence.startSupport.materialsCap},
              std::pair<const char*, int*>{"rune_offers", &sequence.startSupport.runeOffers}}) {
            const auto found = start->find(key);
            if (found == start->end()) continue;
            if (!found->is_number_integer()) {
                return Fail(error, std::string("start_support `") + key + "` must be an integer");
            }
            const auto value = found->get<std::int64_t>();
            if (value < 0 || value > kMaxStartSupportValue) {
                return Fail(error, std::string("start_support `") + key + "` must be 0.." +
                                       std::to_string(kMaxStartSupportValue));
            }
            *target = static_cast<int>(value);
        }
        if (sequence.startSupport.runeOffers > 1) {
            return Fail(error, "start_support `rune_offers` must be 0 (off) or 1 (force the first offer)");
        }
    }

    if (const auto elite = document.find("elite"); elite != document.end()) {
        if (!elite->is_object()) return Fail(error, "`elite` must be an object");
        if (const auto reward = elite->find("reward_materials"); reward != elite->end()) {
            if (!reward->is_number_integer()) {
                return Fail(error, "elite.reward_materials: must be an integer");
            }
            const auto value = reward->get<std::int64_t>();
            if (value < 0 || value > kMaxStartSupportValue) {
                return Fail(error, "elite.reward_materials: must be 0.." + std::to_string(kMaxStartSupportValue));
            }
            sequence.elite.rewardMaterials = static_cast<int>(value);
        }
        if (const auto affixes = elite->find("affixes"); affixes != elite->end()) {
            if (!affixes->is_array()) return Fail(error, "elite.affixes: must be an array");
            int index = 0;
            for (const auto& row : *affixes) {
                const std::string where = "elite.affixes[" + std::to_string(index++) + "]: ";
                if (!row.is_object()) return Fail(error, where + "must be an object");
                FEliteAffix affix;
                for (const auto& [key, target] :
                     {std::pair<const char*, std::string*>{"id", &affix.id},
                      std::pair<const char*, std::string*>{"display_name", &affix.displayName},
                      std::pair<const char*, std::string*>{"text", &affix.text}}) {
                    const auto found = row.find(key);
                    if (found == row.end() || !found->is_string() || found->get<std::string>().empty()) {
                        return Fail(error, where + "`" + key + "` must be a non-empty string");
                    }
                    *target = found->get<std::string>();
                }
                for (const auto& [key, target] :
                     {std::pair<const char*, int*>{"health_percent", &affix.healthPercent},
                      std::pair<const char*, int*>{"attack_bonus", &affix.attackBonus}}) {
                    const auto found = row.find(key);
                    if (found == row.end()) continue;
                    if (!found->is_number_integer()) return Fail(error, where + "`" + key + "` must be an integer");
                    const auto value = found->get<std::int64_t>();
                    if (value < 0 || value > 1000) return Fail(error, where + "`" + key + "` must be 0..1000");
                    *target = static_cast<int>(value);
                }
                if (affix.healthPercent == 0 && affix.attackBonus == 0) {
                    return Fail(error, where + "needs health_percent or attack_bonus above 0");
                }
                for (const auto& existing : sequence.elite.affixes) {
                    if (existing.id == affix.id) return Fail(error, where + "duplicate id `" + affix.id + "`");
                }
                sequence.elite.affixes.push_back(std::move(affix));
            }
        }
    }

    // D-19: the run-end switch exists as data, but its consumer (the run-victory end screen) is WP-19b —
    // until it lands, `true` is a loud boot refusal instead of a silently ignored flag.
    if (const auto run = document.find("run"); run != document.end()) {
        if (!run->is_object()) return Fail(error, "`run` must be an object");
        if (const auto flag = run->find("end_on_final_boss"); flag != run->end()) {
            if (!flag->is_boolean()) return Fail(error, "run `end_on_final_boss` must be a boolean");
            if (flag->get<bool>()) {
                return Fail(error, "end_on_final_boss=true needs the run-victory end screen (WP-19b consumer)");
            }
        }
    }

    if (const auto hint = document.find("next_dungeon_hint"); hint != document.end()) {
        if (!hint->is_string()) return Fail(error, "`next_dungeon_hint` must be a string");
        sequence.nextDungeonHint = hint->get<std::string>();
    }
    return sequence;
}

FRunSequence::FPosition FRunSequence::PositionOf(int cleared) const {
    FPosition position;
    const int total = TotalDungeons();
    if (total <= 0) return position;  // an empty sequence is loader-invalid; never divide by it
    const int safe = std::max(0, cleared);
    position.cycle = safe / total;
    int index = safe % total;
    for (std::size_t theme = 0; theme < themes.size(); ++theme) {
        const int count = static_cast<int>(themes[theme].dungeons.size());
        if (index < count) {
            position.theme = theme;
            position.dungeonInTheme = static_cast<std::size_t>(index);
            return position;
        }
        index -= count;
    }
    // Unreachable while TotalDungeons() is the sum of the theme counts. Clamp to the last dungeon.
    position.theme = themes.size() - 1;
    position.dungeonInTheme = themes.back().dungeons.size() - 1;
    return position;
}

const FRunDungeonDef& FRunSequence::DungeonAt(const FPosition& position) const {
    return themes[position.theme].dungeons[position.dungeonInTheme];
}

int FRunSequence::TotalDungeons() const {
    int total = 0;
    for (const auto& theme : themes) total += static_cast<int>(theme.dungeons.size());
    return total;
}

int FRunSequence::BattleCountAt(const FPosition& position) const {
    int battles = 0;
    for (const auto& slot : DungeonAt(position).rooms) {
        if (slot.type == "battle") ++battles;
    }
    return battles;
}

int FRunSequence::RunCycle(int cleared) const {
    const int total = TotalDungeons();
    return total <= 0 ? 0 : std::max(0, cleared) / total;
}

const FRunRoomSlot* FRunSequence::BattleSlotAt(const FPosition& position, int ordinal) const {
    if (ordinal < 0) return nullptr;
    int seen = 0;
    for (const auto& slot : DungeonAt(position).rooms) {
        if (slot.type != "battle") continue;
        if (seen == ordinal) return &slot;
        ++seen;
    }
    return nullptr;
}

bool FRunSequence::IsBossBattle(const FPosition& position, int ordinal) const {
    const auto* slot = BattleSlotAt(position, ordinal);
    return slot != nullptr && !slot->boss.empty();
}

const FRunRoomSlot* FRunSequence::GapEvent(const FPosition& position, int ordinal) const {
    if (ordinal < 0) return nullptr;
    int seen = 0;
    const auto& rooms = DungeonAt(position).rooms;
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        if (rooms[index].type != "battle") continue;
        if (seen == ordinal) {
            if (index + 1 < rooms.size() && rooms[index + 1].type != "battle") return &rooms[index + 1];
            return nullptr;
        }
        ++seen;
    }
    return nullptr;
}

int FRunSequence::StartMaterials(int startIndex) const {
    if (startIndex <= 0) return 0;
    return std::min(startIndex * startSupport.materialsPerDungeon, startSupport.materialsCap);
}

const FRunSequence* FindRunSequence(const entt::registry& registry) {
    return registry.ctx().find<FRunSequence>();
}

std::optional<FRunSequence::FPosition> RunPosition(const entt::registry& registry) {
    const auto* sequence = FindRunSequence(registry);
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (sequence == nullptr || run == nullptr) return std::nullopt;
    return sequence->PositionOf(run->depth - 1);
}

FCycleScale CycleScaleFor(const entt::registry& registry) {
    FCycleScale scale;
    const auto* sequence = FindRunSequence(registry);
    const auto position = RunPosition(registry);
    if (sequence == nullptr || !position) return scale;
    for (int index = 0; index < position->cycle; ++index) {
        if (sequence->cycles.empty()) break;  // empty table = no cycle scaling (the rollback lever)
        const auto row = sequence->cycles[std::min(static_cast<std::size_t>(index), sequence->cycles.size() - 1)];
        scale.healthPct += row.healthPct;
        scale.attackPct += row.attackPct;
    }
    return scale;
}

FRoomPlan FrameRoomPlan(const entt::registry& registry, int battleOrdinal) {
    // A pure ctx read: the plan is derived from the live run position, the slot and the installed encounter
    // table. The caller (BuildDungeonBattlefield) is the only writer — the boot probe never lands here.
    FRoomPlan plan;
    const auto* sequence = FindRunSequence(registry);
    const auto position = RunPosition(registry);
    if (sequence == nullptr || !position) return plan;
    const FRunRoomSlot* slot = sequence->BattleSlotAt(*position, battleOrdinal);
    if (slot == nullptr) return plan;
    plan.family = slot->family;
    // Boss slots stay on the WP-15 generation pool (encounters never apply to them).
    if (sequence->IsBossBattle(*position, battleOrdinal)) return plan;
    const auto* table = FindEncounterContent(registry);
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (table == nullptr || table->encounters.empty() || run == nullptr) return plan;
    // PositionOf clamps to a valid theme, so themes[position.theme] is always addressable (CycleScaleFor
    // relies on the same invariant).
    const std::string& themeId = sequence->themes[position->theme].id;
    if (const FEncounterDef* def = ResolveEncounter(*table, slot->encounter, themeId, run->seed, run->depth,
                                                    battleOrdinal)) {
        plan.encounterId = def->id;
    }
    return plan;
}

} // namespace game
