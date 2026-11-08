#include "gameplay/dungeon/FEncounterContent.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FRunSequence.h"

namespace game {
namespace {

// ------------------------------------------------------------------------------------------------
// `data/encounters` schema v1 (WP-14a): the OPTIONAL encounter-composition document. Parsed by hand
// over nlohmann (the row shape is nested deeper than FContentLoader projects); every refusal names the
// encounter (or its index) and the field. A present-but-broken file stops the boot (the engine
// ignition precedent for optional tables); an absent one keeps the legacy generation.enemies path.
// ------------------------------------------------------------------------------------------------

constexpr std::string_view kEncounterAssetId = "data/encounters";
constexpr int kEncounterSchemaVersion = 1;

/// `count.max` cap: a battle's enemy count stays bounded (the spawn band and the reachability guarantee
/// are the generator's business past this point).
constexpr int kMaxEncounterEnemies = 8;

/// Role reach thresholds (P14-5 / D-16): a melee skill reaches kMeleeReach tiles, a ranged skill needs
/// kRangedMinReach or more. A role is a skill combination, never an AI profile.
constexpr int kMeleeReach = 1;
constexpr int kRangedMinReach = 2;

/// ResolveEncounter's stream parameters: the depth/room multipliers and the encounter-only salt. The
/// pick stream never aliases the room generator's, so terrain stays byte-identical whatever is picked.
constexpr std::uint32_t kEncounterDepthMix = 0x9E3779B1u;
constexpr std::uint32_t kEncounterRoomMix = 0x7FEB352Du;
constexpr std::uint32_t kEncounterStreamSalt = 0x6C8E9CF5u;
/// The stream gamma (FRoomGenerator's file-local RNG advances its state by this per draw): the discarded
/// warm-up and the pick are the stream's first two steps.
constexpr std::uint32_t kEncounterStreamStep = 0x9E3779B9u;

/// The closed key sets of schema v1 (a new key is a deliberate edit here; WP-14b adds `elite`).
constexpr std::string_view kEncounterDocumentKeys[] = {"schema_version", "encounters", "_comment"};
constexpr std::string_view kEncounterRowKeys[] = {
    "id", "introduced_at_depth", "tags", "count", "slots", "fill", "intent_commit_mix", "_comment"};
constexpr std::string_view kEncounterSlotKeys[] = {"role", "units"};
constexpr std::string_view kEncounterUnitKeys[] = {"unit", "weight"};
constexpr std::string_view kEncounterFillKeys[] = {"units"};

/// ResolveEncounter's pick value: a pure function instead of a copied RNG class. It computes exactly the
/// value the room generator's file-local RNG returns for the *pick* draw — the same seed mix (depth/room
/// multipliers + the encounter-only salt), the same warm-up draw (produced and discarded, so the pick
/// never aliases a generator draw), then the same 32-bit finalizer. Callers reduce it modulo the count.
[[nodiscard]] std::uint32_t EncounterPickHash(std::uint32_t seed, int depth, int room)
{
    std::uint32_t state = seed ^ (static_cast<std::uint32_t>(depth) * kEncounterDepthMix) ^
                          (static_cast<std::uint32_t>(room) * kEncounterRoomMix) ^ kEncounterStreamSalt;
    state += kEncounterStreamStep;  // the warm-up draw (produced and discarded, as before)
    state += kEncounterStreamStep;  // the pick draw
    std::uint32_t z = state;
    z = (z ^ (z >> 16)) * 0x21F0AAADu;
    z = (z ^ (z >> 15)) * 0x735A2D97u;
    return z ^ (z >> 15);
}

/// True when `role` is one of the four closed role constants.
[[nodiscard]] bool IsKnownEncounterRole(std::string_view role)
{
    return role == kEncounterRoleMelee || role == kEncounterRoleRanged || role == kEncounterRoleSupport ||
           role == kEncounterRoleArtillery;
}

/// The first key of `object` outside `allowed` (schema v1 is closed), or nullopt.
[[nodiscard]] std::optional<std::string> UnknownKey(const nlohmann::json& object,
                                                    std::span<const std::string_view> allowed)
{
    for (const auto& item : object.items())
    {
        const std::string_view key = item.key();
        if (std::ranges::find(allowed, key) == allowed.end()) return std::string(key);
    }
    return std::nullopt;
}

/// Decodes the `{"unit": "...", "weight": N}` rows shared by a role slot's pool and `fill`. `where` names
/// the owning encounter/slot/fill so every refusal points at the row.
[[nodiscard]] bool ParseUnitChoices(const std::string& where, const nlohmann::json& units,
                                    std::vector<FEncounterUnit>& out, std::string& error)
{
    out.clear();
    out.reserve(units.size());
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const nlohmann::json& choice = units[index];
        const std::string choiceWhere = where + " unit " + std::to_string(index);
        if (!choice.is_object())
        {
            error = choiceWhere + ": must be an object with 'unit' and an optional 'weight'";
            return false;
        }
        if (const std::optional<std::string> unknown = UnknownKey(choice, kEncounterUnitKeys))
        {
            error = choiceWhere + ": unknown key '" + *unknown + "' (unit, weight)";
            return false;
        }
        const auto unitIt = choice.find("unit");
        if (unitIt == choice.end() || !unitIt->is_string() || unitIt->get_ref<const std::string&>().empty())
        {
            error = choiceWhere + ": 'unit' must be a nonempty string";
            return false;
        }
        FEncounterUnit decoded;
        decoded.unit = unitIt->get<std::string>();
        if (const auto weightIt = choice.find("weight"); weightIt != choice.end())
        {
            if (!weightIt->is_number_integer() || weightIt->get<std::int64_t>() < 1 ||
                weightIt->get<std::int64_t>() > std::numeric_limits<int>::max())
            {
                error = choiceWhere + ": 'weight' must be an integer >= 1";
                return false;
            }
            decoded.weight = static_cast<int>(weightIt->get<std::int64_t>());
        }
        out.push_back(std::move(decoded));
    }
    return true;
}

// --- role predicates (P14-5): authored as skill combinations, never as AI profiles -----------------

/// melee: carries a hostile-target skill that reaches at most kMeleeReach tiles.
[[nodiscard]] bool HasMeleeSkill(const gameplay::FUnitContent& unit, const gameplay::FContentRegistry& content)
{
    for (const std::string& skillId : unit.skillIds)
    {
        const gameplay::FSkillContent* skill = content.skills.find(skillId);
        if (skill != nullptr && skill->target == "enemy" && skill->range <= kMeleeReach) return true;
    }
    return false;
}

/// ranged: a hostile-target unit-mode skill at kRangedMinReach tiles or farther (the FIntent mode rule).
[[nodiscard]] bool HasRangedSkill(const gameplay::FUnitContent& unit, const gameplay::FContentRegistry& content)
{
    for (const std::string& skillId : unit.skillIds)
    {
        const gameplay::FSkillContent* skill = content.skills.find(skillId);
        if (skill != nullptr && skill->target == "enemy" && skill->range >= kRangedMinReach &&
            IntentModeOf(skill->target, skill->shape) == EIntentMode::Unit)
        {
            return true;
        }
    }
    return false;
}

/// support: a skill aimed at an ally.
[[nodiscard]] bool HasSupportSkill(const gameplay::FUnitContent& unit, const gameplay::FContentRegistry& content)
{
    for (const std::string& skillId : unit.skillIds)
    {
        const gameplay::FSkillContent* skill = content.skills.find(skillId);
        if (skill != nullptr && skill->target == "ally") return true;
    }
    return false;
}

/// artillery: a tile-commit skill — IntentModeOf is the single mode rule (D-16), never re-implemented.
[[nodiscard]] bool HasArtillerySkill(const gameplay::FUnitContent& unit, const gameplay::FContentRegistry& content)
{
    for (const std::string& skillId : unit.skillIds)
    {
        const gameplay::FSkillContent* skill = content.skills.find(skillId);
        if (skill != nullptr && IntentModeOf(skill->target, skill->shape) == EIntentMode::Tile) return true;
    }
    return false;
}

/// The role -> predicate dispatch. An unknown role is unsatisfiable (the parser never installs one).
[[nodiscard]] bool UnitSatisfiesRole(std::string_view role, const gameplay::FUnitContent& unit,
                                     const gameplay::FContentRegistry& content)
{
    if (role == kEncounterRoleMelee) return HasMeleeSkill(unit, content);
    if (role == kEncounterRoleRanged) return HasRangedSkill(unit, content);
    if (role == kEncounterRoleSupport) return HasSupportSkill(unit, content);
    if (role == kEncounterRoleArtillery) return HasArtillerySkill(unit, content);
    return false;
}

/// The dynamic pool gate: the introduced depth has passed AND (no tags OR the theme is one of them).
[[nodiscard]] bool EncounterEligible(const FEncounterDef& encounter, std::string_view themeId, int depth)
{
    if (encounter.introducedAtDepth > depth) return false;
    if (encounter.tags.empty()) return true;
    return std::ranges::find(encounter.tags, themeId) != encounter.tags.end();
}

/// Parses one encounter's `count` into `def.count`: [min, max], 1 <= min <= max <= kMaxEncounterEnemies,
/// and min >= the slot count — every role slot is always placed, so the total may not undershoot them.
[[nodiscard]] bool ParseCount(const std::string& where, const nlohmann::json& row, std::size_t slotCount,
                              FEncounterDef& def, std::string& error)
{
    const std::string countRule = where + ": 'count' must be [min, max] with 1 <= min <= max <= " +
                                  std::to_string(kMaxEncounterEnemies);
    const auto countIt = row.find("count");
    if (countIt == row.end() || !countIt->is_array() || countIt->size() != 2 ||
        !(*countIt)[0].is_number_integer() || !(*countIt)[1].is_number_integer())
    {
        error = countRule;
        return false;
    }
    const std::int64_t countMin = (*countIt)[0].get<std::int64_t>();
    const std::int64_t countMax = (*countIt)[1].get<std::int64_t>();
    if (countMin < 1 || countMin > countMax || countMax > kMaxEncounterEnemies)
    {
        error = countRule;
        return false;
    }
    if (countMin < static_cast<std::int64_t>(slotCount))
    {
        error = where + ": 'count' min " + std::to_string(countMin) + " is below the " +
                std::to_string(slotCount) + " role slot(s); every slot is always placed";
        return false;
    }
    def.count.min = static_cast<int>(countMin);
    def.count.max = static_cast<int>(countMax);
    return true;
}

/// Parses `slots` into `def.slots` (placement order; slot 0 seats the leader) and hands the artillery
/// slot count back through `artillerySlots` for the `intent_commit_mix` check. Each slot needs a known
/// role and a nonempty weighted pool; the slot keys are closed (an unknown key is refused).
[[nodiscard]] bool ParseSlots(const std::string& where, const nlohmann::json& row, FEncounterDef& def,
                              int& artillerySlots, std::string& error)
{
    const auto slots = row.find("slots");
    if (slots == row.end() || !slots->is_array() || slots->empty())
    {
        error = where + ": 'slots' must be a nonempty array of role slots";
        return false;
    }
    def.slots.reserve(slots->size());
    artillerySlots = 0;
    for (std::size_t slotIndex = 0; slotIndex < slots->size(); ++slotIndex)
    {
        const nlohmann::json& slotRow = (*slots)[slotIndex];
        const std::string slotWhere = where + " slot " + std::to_string(slotIndex);
        if (!slotRow.is_object())
        {
            error = slotWhere + ": must be an object with 'role' and 'units'";
            return false;
        }
        if (const std::optional<std::string> unknown = UnknownKey(slotRow, kEncounterSlotKeys))
        {
            error = slotWhere + ": unknown key '" + *unknown + "' (role, units)";
            return false;
        }
        const auto roleIt = slotRow.find("role");
        if (roleIt == slotRow.end() || !roleIt->is_string() ||
            !IsKnownEncounterRole(roleIt->get_ref<const std::string&>()))
        {
            error = slotWhere + ": 'role' must be one of melee, ranged, support, artillery";
            return false;
        }
        const auto unitsIt = slotRow.find("units");
        if (unitsIt == slotRow.end() || !unitsIt->is_array() || unitsIt->empty())
        {
            error = slotWhere + ": 'units' must be a nonempty array of unit choices";
            return false;
        }
        FEncounterSlot slot;
        slot.role = roleIt->get<std::string>();
        if (!ParseUnitChoices(slotWhere, *unitsIt, slot.units, error)) return false;
        if (slot.role == kEncounterRoleArtillery) ++artillerySlots;
        def.slots.push_back(std::move(slot));
    }
    return true;
}

/// Parses `fill` into `def.fill`. It is required and nonempty exactly when `count.max` exceeds the slot
/// count — every role slot is always placed, so any enemy past them comes from the fill pool. An
/// authored-but-not-needed fill is still decoded and checked.
[[nodiscard]] bool ParseFill(const std::string& where, const nlohmann::json& row, FEncounterDef& def,
                             std::string& error)
{
    const bool fillNeeded = def.count.max > static_cast<int>(def.slots.size());
    const auto fillIt = row.find("fill");
    if (fillIt == row.end())
    {
        if (fillNeeded)
        {
            error = where + ": 'fill' is required: 'count' max " + std::to_string(def.count.max) +
                    " exceeds the " + std::to_string(def.slots.size()) + " role slot(s)";
            return false;
        }
        return true;
    }
    const std::string fillWhere = where + " fill";
    if (!fillIt->is_object())
    {
        error = fillWhere + ": must be an object with a 'units' array";
        return false;
    }
    if (const std::optional<std::string> unknown = UnknownKey(*fillIt, kEncounterFillKeys))
    {
        error = fillWhere + ": unknown key '" + *unknown + "' (units)";
        return false;
    }
    const auto unitsIt = fillIt->find("units");
    if (unitsIt == fillIt->end() || !unitsIt->is_array())
    {
        error = fillWhere + ": 'units' must be an array of unit choices";
        return false;
    }
    if (fillNeeded && unitsIt->empty())
    {
        error = fillWhere + ": 'units' must be nonempty: 'count' max " + std::to_string(def.count.max) +
                " exceeds the " + std::to_string(def.slots.size()) + " role slot(s)";
        return false;
    }
    return ParseUnitChoices(fillWhere, *unitsIt, def.fill, error);
}

/// Parses `intent_commit_mix` (P14-5): required, >= 1, and satisfiable — at most one per artillery slot.
[[nodiscard]] bool ParseIntentCommitMix(const std::string& where, const nlohmann::json& row, int artillerySlots,
                                        FEncounterDef& def, std::string& error)
{
    const auto mixIt = row.find("intent_commit_mix");
    if (mixIt == row.end() || !mixIt->is_number_integer() || mixIt->get<std::int64_t>() < 1)
    {
        error = where + ": 'intent_commit_mix' must be an integer >= 1";
        return false;
    }
    if (mixIt->get<std::int64_t>() > artillerySlots)
    {
        error = where + ": 'intent_commit_mix' " + std::to_string(mixIt->get<std::int64_t>()) +
                " exceeds the " + std::to_string(artillerySlots) + " artillery slot(s)";
        return false;
    }
    def.intentCommitMix = static_cast<int>(mixIt->get<std::int64_t>());
    return true;
}

/// Parses the optional `introduced_at_depth` dynamic-pool gate (default 1 = eligible everywhere).
[[nodiscard]] bool ParseIntroducedAtDepth(const std::string& where, const nlohmann::json& row,
                                          FEncounterDef& def, std::string& error)
{
    const auto depthIt = row.find("introduced_at_depth");
    if (depthIt == row.end()) return true;
    if (!depthIt->is_number_integer() || depthIt->get<std::int64_t>() < 1 ||
        depthIt->get<std::int64_t>() > std::numeric_limits<int>::max())
    {
        error = where + ": 'introduced_at_depth' must be an integer >= 1";
        return false;
    }
    def.introducedAtDepth = static_cast<int>(depthIt->get<std::int64_t>());
    return true;
}

/// Parses the optional `tags` theme filter (default none = every theme); entries are nonempty ids.
[[nodiscard]] bool ParseTags(const std::string& where, const nlohmann::json& row, FEncounterDef& def,
                             std::string& error)
{
    const auto tagsIt = row.find("tags");
    if (tagsIt == row.end()) return true;
    if (!tagsIt->is_array())
    {
        error = where + ": 'tags' must be an array of nonempty theme ids";
        return false;
    }
    for (std::size_t tagIndex = 0; tagIndex < tagsIt->size(); ++tagIndex)
    {
        const nlohmann::json& tag = (*tagsIt)[tagIndex];
        if (!tag.is_string() || tag.get_ref<const std::string&>().empty())
        {
            error = where + ": 'tags' must be an array of nonempty theme ids";
            return false;
        }
        def.tags.push_back(tag.get<std::string>());
    }
    return true;
}

/// Parses one row whose `id` is already known (presence, type and uniqueness live in
/// ParseEncounterContent): the closed row keys, then slots -> count -> fill -> intent_commit_mix ->
/// introduced_at_depth -> tags -> `_comment`. The fixed order keeps every row refusal stable.
[[nodiscard]] bool ParseEncounterRow(const std::string& where, const nlohmann::json& row, FEncounterDef& def,
                                     std::string& error)
{
    if (const std::optional<std::string> unknown = UnknownKey(row, kEncounterRowKeys))
    {
        error = where + ": unknown key '" + *unknown + "'";
        return false;
    }
    int artillerySlots = 0;
    if (!ParseSlots(where, row, def, artillerySlots, error)) return false;
    if (!ParseCount(where, row, def.slots.size(), def, error)) return false;
    if (!ParseFill(where, row, def, error)) return false;
    if (!ParseIntentCommitMix(where, row, artillerySlots, def, error)) return false;
    if (!ParseIntroducedAtDepth(where, row, def, error)) return false;
    if (!ParseTags(where, row, def, error)) return false;
    const auto commentIt = row.find("_comment");
    if (commentIt != row.end() && !commentIt->is_string())
    {
        error = where + ": '_comment' must be a string";
        return false;
    }
    return true;
}

} // namespace

std::optional<FEncounterContent> ParseEncounterContent(const nlohmann::json& document, std::string& error)
{
    if (!document.is_object())
    {
        error = "the encounters document must be an object";
        return std::nullopt;
    }
    const auto schema = document.find("schema_version");
    if (schema == document.end() || !schema->is_number_integer() ||
        schema->get<std::int64_t>() != kEncounterSchemaVersion)
    {
        error = "`schema_version` must be the integer 1";
        return std::nullopt;
    }
    if (const std::optional<std::string> unknown = UnknownKey(document, kEncounterDocumentKeys))
    {
        error = "unknown top-level key '" + *unknown + "' (schema v1: schema_version, encounters, _comment)";
        return std::nullopt;
    }
    if (const auto comment = document.find("_comment"); comment != document.end() && !comment->is_string())
    {
        error = "`_comment` must be a string";
        return std::nullopt;
    }
    const auto encounters = document.find("encounters");
    if (encounters == document.end() || !encounters->is_array())
    {
        error = "`encounters` must be an array (an empty array is a valid no-composition file)";
        return std::nullopt;
    }

    FEncounterContent table;
    table.schemaVersion = kEncounterSchemaVersion;
    std::unordered_set<std::string> seenIds;  // membership only, never iterated (R16)

    for (std::size_t index = 0; index < encounters->size(); ++index)
    {
        const nlohmann::json& row = (*encounters)[index];
        if (!row.is_object())
        {
            error = "encounter #" + std::to_string(index) + " must be an object";
            return std::nullopt;
        }
        const auto idIt = row.find("id");
        if (idIt == row.end() || !idIt->is_string() || idIt->get_ref<const std::string&>().empty())
        {
            error = "encounter #" + std::to_string(index) + ": 'id' must be a nonempty string";
            return std::nullopt;
        }
        const std::string id = idIt->get<std::string>();
        const std::string where = "encounter '" + id + "'";
        if (!seenIds.insert(id).second)
        {
            error = where + ": duplicate id";
            return std::nullopt;
        }
        FEncounterDef def;
        def.id = id;
        if (!ParseEncounterRow(where, row, def, error)) return std::nullopt;

        table.encounters.push_back(std::move(def));
    }
    return table;
}

bool CheckEncounterContent(const FEncounterContent& table, const gameplay::FContentRegistry& content,
                           std::string& error)
{
    for (const FEncounterDef& encounter : table.encounters)
    {
        for (std::size_t slotIndex = 0; slotIndex < encounter.slots.size(); ++slotIndex)
        {
            const FEncounterSlot& slot = encounter.slots[slotIndex];
            for (const FEncounterUnit& choice : slot.units)
            {
                const gameplay::FUnitContent* unit = content.units.find(choice.unit);
                if (unit == nullptr)
                {
                    error = "encounter '" + encounter.id + "' slot " + std::to_string(slotIndex) + " ('" +
                            slot.role + "'): unit '" + choice.unit + "' is not in the unit table";
                    return false;
                }
                if (!UnitSatisfiesRole(slot.role, *unit, content))
                {
                    error = "encounter '" + encounter.id + "' slot " + std::to_string(slotIndex) + " ('" +
                            slot.role + "'): unit '" + choice.unit + "' has no skill that matches the role";
                    return false;
                }
            }
        }
        // Fill units carry no role: only existence is required.
        for (const FEncounterUnit& choice : encounter.fill)
        {
            if (content.units.find(choice.unit) == nullptr)
            {
                error = "encounter '" + encounter.id + "' fill: unit '" + choice.unit + "' is not in the unit table";
                return false;
            }
        }
    }
    return true;
}

const FEncounterDef* FEncounterContent::Find(std::string_view id) const
{
    for (const FEncounterDef& encounter : encounters)
    {
        if (encounter.id == id) return &encounter;
    }
    return nullptr;
}

const FEncounterContent* FindEncounterContent(const entt::registry& registry)
{
    return registry.ctx().find<FEncounterContent>();
}

const FRoomPlan* FindRoomPlan(const entt::registry& registry)
{
    return registry.ctx().find<FRoomPlan>();
}

bool LoadEncounterContent(entt::registry& registry, const FAssetManager& assets)
{
    const std::optional<std::string> path = assets.resolvePath(kEncounterAssetId);
    if (!path)
    {
        // The optional-file contract: absent = the legacy `generation.enemies` path (no composition, no
        // boss interaction). Erase a stale table so a re-initialize really falls back.
        registry.ctx().erase<FEncounterContent>();
        LOG_INFO("[encounters] no '{}' document; the legacy generation.enemies path is used.", kEncounterAssetId);
        return true;
    }
    const auto document = FJsonLoader::Load(*path, kEncounterSchemaVersion);
    if (!document) return false;  // present but unreadable: the reason is already logged
    std::string error;
    std::optional<FEncounterContent> table = ParseEncounterContent(*document, error);
    if (!table)
    {
        LOG_ERROR("[encounters] {}", error);
        return false;
    }
    registry.ctx().insert_or_assign<FEncounterContent>(std::move(*table));
    return true;
}

bool ValidateEncounterContent(entt::registry& registry)
{
    const FEncounterContent* table = registry.ctx().find<FEncounterContent>();
    if (table == nullptr) return true;  // no file: the legacy path has nothing to cross-check
    const gameplay::FContentRegistry* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (content == nullptr)
    {
        LOG_ERROR("[encounters] the content registry is missing; the encounter table cannot be checked.");
        return false;
    }
    std::string error;
    if (!CheckEncounterContent(*table, *content, error))
    {
        LOG_ERROR("[encounters] {}", error);
        return false;
    }
    LOG_INFO("[encounters] loaded={} encounter(s)", table->encounters.size());
    return true;
}

const FEncounterDef* ResolveEncounter(const FEncounterContent& table, std::string_view encounterId,
                                      std::string_view themeId, std::uint32_t seed, int depth, int room)
{
    // A named encounter is trusted as-is: eligibility is ValidateEncounterCoverage's boot-time concern.
    if (!encounterId.empty()) return table.Find(encounterId);
    std::vector<const FEncounterDef*> eligible;
    for (const FEncounterDef& encounter : table.encounters)
    {
        if (EncounterEligible(encounter, themeId, depth)) eligible.push_back(&encounter);
    }
    if (eligible.empty()) return nullptr;
    return eligible[EncounterPickHash(seed, depth, room) % eligible.size()];
}

bool ValidateEncounterCoverage(const FEncounterContent& table, const FRunSequence& sequence, std::string& error)
{
    if (table.encounters.empty()) return true;  // no composition: the legacy path covers every slot
    for (int dungeonIndex = 0; dungeonIndex < sequence.TotalDungeons(); ++dungeonIndex)
    {
        const FRunSequence::FPosition position = sequence.PositionOf(dungeonIndex);
        const int depth = dungeonIndex + 1;
        const std::string& themeId = sequence.themes[position.theme].id;
        const std::string dungeonName = sequence.DungeonAt(position).name;
        const int battles = sequence.BattleCountAt(position);
        for (int ordinal = 0; ordinal < battles; ++ordinal)
        {
            const FRunRoomSlot* slot = sequence.BattleSlotAt(position, ordinal);
            if (slot == nullptr || sequence.IsBossBattle(position, ordinal)) continue;  // bosses stay WP-15
            const std::string where = "dungeon '" + dungeonName + "' battle slot " + std::to_string(ordinal) +
                                      " (depth " + std::to_string(depth) + ", theme '" + themeId + "')";
            if (!slot->encounter.empty())
            {
                const FEncounterDef* named = table.Find(slot->encounter);
                if (named == nullptr)
                {
                    error = where + ": encounter '" + slot->encounter + "' does not exist";
                    return false;
                }
                if (named->introducedAtDepth > depth)
                {
                    error = where + ": encounter '" + named->id + "' is introduced at depth " +
                            std::to_string(named->introducedAtDepth) + ", deeper than depth " +
                            std::to_string(depth);
                    return false;
                }
                if (!EncounterEligible(*named, themeId, depth))
                {
                    error = where + ": encounter '" + named->id + "' does not allow theme '" + themeId + "'";
                    return false;
                }
            }
            else if (std::ranges::none_of(table.encounters, [&](const FEncounterDef& encounter) {
                         return EncounterEligible(encounter, themeId, depth);
                     }))
            {
                error = where + ": no encounter is eligible (introduced_at_depth <= " + std::to_string(depth) +
                        ", tags allow '" + themeId + "')";
                return false;
            }
        }
    }
    return true;
}

void ApplyEncounter(gameplay::FRoomGenConfig& config, const FEncounterDef& encounter,
                    const gameplay::FContentRegistry& content)
{
    const auto displayName = [&content](const FEncounterUnit& choice) {
        const gameplay::FUnitContent* unit = content.units.find(choice.unit);
        return unit != nullptr ? unit->displayName : choice.unit;
    };
    config.enemyCount = encounter.count;
    config.roleSlots.clear();
    config.roleSlots.reserve(encounter.slots.size());
    for (const FEncounterSlot& slot : encounter.slots)
    {
        gameplay::FRoleSlotSpawn spawn;
        spawn.role = slot.role;
        spawn.units.reserve(slot.units.size());
        for (const FEncounterUnit& choice : slot.units)
        {
            spawn.units.push_back(gameplay::FEnemyChoice{choice.unit, displayName(choice), choice.weight});
        }
        config.roleSlots.push_back(std::move(spawn));
    }
    config.fill.clear();
    config.fill.reserve(encounter.fill.size());
    for (const FEncounterUnit& choice : encounter.fill)
    {
        config.fill.push_back(gameplay::FEnemyChoice{choice.unit, displayName(choice), choice.weight});
    }
}

} // namespace game
