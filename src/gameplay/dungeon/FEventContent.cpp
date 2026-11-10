#include "gameplay/dungeon/FEventContent.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
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
#include "core/data/FContentLoader.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FEventEffects.h"
#include "gameplay/dungeon/FRoomTypeRegistry.h"

namespace game {
namespace {

// ------------------------------------------------------------------------------------------------
// `data/events` schema (WP-09 §1). Field order IS the projection order FContentLoader uses; the
// `k*Field*` indices are the decode order below. Sub-schemas are declared before the field arrays that
// point at them (constexpr address init, WP-26).
// ------------------------------------------------------------------------------------------------

constexpr FContentField kEventCheckFields[] = {
    {"stat", EContentFieldType::Id, true},
    {"difficulty", EContentFieldType::Number, true},
};
constexpr std::size_t kCheckFieldStat = 0;
constexpr std::size_t kCheckFieldDifficulty = 1;
static_assert(std::size(kEventCheckFields) == kCheckFieldDifficulty + 1,
              "kEventCheckFields and the kCheckField* indices drifted apart");

constexpr FContentSchema kEventCheckSchema{"", "", "", 1, kEventCheckFields};

constexpr FContentField kEventChoiceFields[] = {
    {"text", EContentFieldType::Text, true},
    {"check", EContentFieldType::Object, false, &kEventCheckSchema},
    {"effects", EContentFieldType::ObjectArray, false, &gameplay::kEffectSchema},
    {"result_text", EContentFieldType::Text, true},
};
constexpr std::size_t kChoiceFieldText = 0;
constexpr std::size_t kChoiceFieldCheck = 1;
constexpr std::size_t kChoiceFieldEffects = 2;
constexpr std::size_t kChoiceFieldResultText = 3;
static_assert(std::size(kEventChoiceFields) == kChoiceFieldResultText + 1,
              "kEventChoiceFields and the kChoiceField* indices drifted apart");

constexpr FContentSchema kEventChoiceSchema{"", "", "", 1, kEventChoiceFields};

constexpr FContentField kEventFields[] = {
    {"id", EContentFieldType::Text, true},
    {"title", EContentFieldType::Text, true},
    {"body", EContentFieldType::Text, true},
    {"tags", EContentFieldType::IdArray, false},
    {"choices", EContentFieldType::ObjectArray, true, &kEventChoiceSchema, kEventMaxChoices},
};
constexpr std::size_t kEventFieldId = 0;
constexpr std::size_t kEventFieldTitle = 1;
constexpr std::size_t kEventFieldBody = 2;
constexpr std::size_t kEventFieldTags = 3;
constexpr std::size_t kEventFieldChoices = 4;
static_assert(std::size(kEventFields) == kEventFieldChoices + 1,
              "kEventFields and the kEventField* indices drifted apart");

constexpr FContentSchema kEventSchema{"data/events", "events", "id", kEventSchemaVersion, kEventFields};

/// `data/dungeon` document version the slot cross-check reads. InitializeDungeon requires the same
/// version; the check below only skips the walk when the document is absent, never guesses a version.
constexpr int kDungeonDocumentSchemaVersion = 2;

/// A percent authored as a decimal fraction may land a hair off a whole number in binary (0.07 × 100
/// lands one ULP above 7), so the rounding warning uses a tolerance instead of an exact compare.
constexpr double kSeedPercentEpsilon = 1e-6;

/// The seed's fixed strings: the pre-WP-09 shrine/cache texts (DungeonTests checks them verbatim).
constexpr std::string_view kSeedTitle = "A Quiet Shrine";
constexpr std::string_view kSeedBody = "A quiet shrine offers shelter, or a sealed cache of reinforcement materials.";
constexpr std::string_view kSeedRestTextPrefix = "Rest at the shrine  /  heal ";
constexpr std::string_view kSeedRestTextSuffix = " HP";
constexpr std::string_view kSeedRestResult = "The party rests at the shrine.";
constexpr std::string_view kSeedCacheTextPrefix = "Open the cache  /  gain ";
constexpr std::string_view kSeedCacheTextSuffix = " materials";
constexpr std::string_view kSeedCacheCostSuffix = "  /  costs party HP";
/// The amounts are not restated here: the run flow appends the applied outcome ("party HP -N, +M materials").
constexpr std::string_view kSeedCacheResult = "The lock gives way; the cache is yours.";

[[nodiscard]] bool IsKnownCheckStat(std::string_view stat) {
    return std::ranges::find(kEventCheckStats, stat) != std::end(kEventCheckStats);
}

/// One nested effect row -> FEffectSpec by the fixed kEffectField* projection (the same decode
/// DecodeHooks does for hooks; a new effect key is one edit in FEffectContent.h, never here).
[[nodiscard]] gameplay::FEffectSpec DecodeEffectRow(const FContentRow& row) {
    gameplay::FEffectSpec spec;
    spec.kind = row.text(gameplay::kEffectFieldKind);
    spec.amount = row.integer(gameplay::kEffectFieldAmount, 0);
    spec.statusId = row.text(gameplay::kEffectFieldStatusId);
    spec.duration = row.integer(gameplay::kEffectFieldDuration, 0);
    spec.magnitude = row.integer(gameplay::kEffectFieldMagnitude, 0);
    spec.distance = row.integer(gameplay::kEffectFieldDistance, 0);
    spec.from = row.text(gameplay::kEffectFieldFrom);
    spec.stat = row.text(gameplay::kEffectFieldStat);
    spec.pool = row.text(gameplay::kEffectFieldPool);
    spec.member = row.text(gameplay::kEffectFieldMember);
    spec.text = row.text(gameplay::kEffectFieldText);
    return spec;
}

/// True when `pool` selects at least one catalog event. The seed is represented by its constants — it is
/// built from `data/dungeon balance` at run time, so the boot (which runs before InitializeDungeon) can
/// only know its fixed tag (lead decision: seed = id kLegacySeedEventId, tags {kLegacySeedPool}).
[[nodiscard]] bool PoolHasAnyEvent(std::span<const FEventDef> authored, std::string_view pool) {
    if (pool.empty() || pool == kLegacySeedPool) return true;  // "" = every event
    return std::ranges::any_of(authored, [pool](const FEventDef& event) { return EventInPool(event, pool); });
}

/// The seed warnings' memory (WP-09h #7): ctx-owned instead of function-local statics, so a fresh registry
/// (or a re-initialized dungeon whose recovery changed) warns again for a new fraction, while the repeated
/// catalog reads of one unchanged fraction stay at one line. Values = the last warned fraction.
struct FEventSeedWarnings {
    std::optional<double> nonWholeFraction;
    std::optional<double> zeroFraction;
};

} // namespace

std::optional<std::vector<FEventDef>> LoadEventDefs(const FAssetManager& assets) {
    // A missing `data/events` is a boot failure (WP-09h #5): the document is required like every other
    // content table — zero authored events is expressed by `"events": []`, never by a missing file — because
    // a packaging slip that drops it would otherwise silently delete every authored event behind a single
    // WARN (WP-23 authors 12 events). `resolvePath` is the existence probe so the loader does not log a
    // second, misleading reason; the ERROR below is the one reason. A present document is loaded (and later
    // validated) like any required table: `"events": []` is the normal zero-event document, a
    // present-but-broken file is unusable.
    if (!assets.resolvePath(kEventSchema.assetId).has_value()) {
        LOG_ERROR("event content: no 'data/events' document; the event table is required (write "
                  "\"events\": [] for zero authored events).");
        return std::nullopt;
    }
    const std::optional<std::vector<FContentRow>> rows = FContentLoader::load(assets, kEventSchema);
    if (!rows) return std::nullopt;  // present but broken: the reason is already logged

    // Zero events is valid: FContentLoader projects `"events": []` into an empty row list, so the W9
    // shipped file (`{"schema_version":1,"events":[]}`) is a normal document, not an error.
    std::vector<FEventDef> events;
    events.reserve(rows->size());
    std::unordered_set<std::string> seenIds;  // insert-only: membership only, never iterated (R16)
    for (const FContentRow& row : *rows) {
        FEventDef event;
        event.id = row.text(kEventFieldId);
        // FContentLoader already rejects empty and duplicate ids; restated here so the catalog semantics
        // never depend on the loader's internals.
        if (event.id.empty()) {
            LOG_ERROR("event content: an event has an empty id.");
            return std::nullopt;
        }
        if (!seenIds.insert(event.id).second) {
            LOG_ERROR("event content: duplicate event id '{}'.", event.id);
            return std::nullopt;
        }
        event.title = row.text(kEventFieldTitle);
        event.body = row.text(kEventFieldBody);
        event.tags = row.textArray(kEventFieldTags);

        const std::span<const FContentRow> choices = row.objectArray(kEventFieldChoices);
        if (choices.size() < kEventMinChoices || choices.size() > kEventMaxChoices) {
            LOG_ERROR("event content: event '{}' has {} choices; the schema allows {}..{}.", event.id,
                      choices.size(), kEventMinChoices, kEventMaxChoices);
            return std::nullopt;
        }
        event.choices.reserve(choices.size());
        for (std::size_t choiceIndex = 0; choiceIndex < choices.size(); ++choiceIndex) {
            const FContentRow& choiceRow = choices[choiceIndex];
            FEventChoice choice;
            choice.text = choiceRow.text(kChoiceFieldText);
            // `check` is an optional Object: the loader projects an absent one as a dense default row
            // (stat "", difficulty 0), so an empty stat is the absence signal. A present check must carry
            // `stat` (the loader refuses the key missing), and an authored empty stat is rejected below
            // whenever it is detectable. FContentRow stores no presence flag, so `{"stat":"","difficulty":0}`
            // projects byte-identically to an absent check and stays "no gate" — the one shape the dense
            // projection cannot tell apart.
            if (const FContentRow* checkRow = choiceRow.object(kChoiceFieldCheck)) {
                const std::string stat = checkRow->text(kCheckFieldStat);
                if (!stat.empty()) {
                    if (!IsKnownCheckStat(stat)) {
                        LOG_ERROR("event content: event '{}' choice {} check stat '{}' is not one of the "
                                  "six upgrade stats.", event.id, choiceIndex, stat);
                        return std::nullopt;
                    }
                    const double difficulty = checkRow->number(kCheckFieldDifficulty, 0.0);
                    if (std::floor(difficulty) != difficulty) {
                        LOG_ERROR("event content: event '{}' choice {} check difficulty {} is not a whole "
                                  "number.", event.id, choiceIndex, difficulty);
                        return std::nullopt;
                    }
                    const int whole = static_cast<int>(difficulty);
                    if (whole < kEventCheckDifficultyMin || whole > kEventCheckDifficultyMax) {
                        LOG_ERROR("event content: event '{}' choice {} check difficulty {} is outside "
                                  "{}..{}.", event.id, choiceIndex, whole, kEventCheckDifficultyMin,
                                  kEventCheckDifficultyMax);
                        return std::nullopt;
                    }
                    choice.check = FEventCheck{stat, whole};
                } else if (checkRow->number(kCheckFieldDifficulty, 0.0) != 0.0) {
                    // The detectable authored empty-stat shape: a check with a set difficulty. Refuse it —
                    // only the absent-check default row (difficulty 0) may carry an empty stat.
                    LOG_ERROR("event content: event '{}' choice {} has a check with an empty stat; name one "
                              "of the six stats or remove the check.", event.id, choiceIndex);
                    return std::nullopt;
                }
            }
            for (const FContentRow& effectRow : choiceRow.objectArray(kChoiceFieldEffects)) {
                gameplay::FEffectSpec effect = DecodeEffectRow(effectRow);
                if (effect.kind.empty()) {
                    LOG_ERROR("event content: event '{}' choice {} has an effect with an empty kind.",
                              event.id, choiceIndex);
                    return std::nullopt;
                }
                choice.effects.push_back(std::move(effect));
            }
            choice.resultText = choiceRow.text(kChoiceFieldResultText);
            event.choices.push_back(std::move(choice));
        }
        events.push_back(std::move(event));
    }
    return std::move(events);
}

bool LoadEventContent(entt::registry& registry, const FAssetManager& assets) {
    std::optional<std::vector<FEventDef>> events = LoadEventDefs(assets);
    if (!events) return false;  // the reason is already logged
    FEventTable table;
    table.events = std::move(*events);
    const std::size_t count = table.events.size();
    registry.ctx().insert_or_assign<FEventTable>(std::move(table));  // replace any previous table
    LOG_INFO("[event] loaded {} authored events", count);
    return true;
}

std::vector<FEventDef> BuildLegacySeedEvents(entt::registry& registry) {
    // The numbers have one canonical source (D-3): `data/dungeon balance` + `recovery`. The seed is
    // rebuilt on every catalog read — never cached in FEventTable — because the boot (FGameplayServices)
    // runs before InitializeDungeon and a re-initialized dungeon must be reflected immediately.
    const int heal = DungeonBalance(registry, "event_heal");
    const int materials = DungeonBalance(registry, "event_materials");
    const double chestFraction = DungeonRecovery(registry).eventChestHpCostFraction;
    const double chestPercent = chestFraction * 100.0;
    const int chestCostPercent = static_cast<int>(std::lround(chestPercent));
    // The warning memory is ctx-owned (WP-09h #7), never a function-local static: a second registry in the
    // same process (or a recovered dungeon) warns again, while repeated reads of one fraction stay quiet.
    auto* warnings = registry.ctx().find<FEventSeedWarnings>();
    if (warnings == nullptr) warnings = &registry.ctx().emplace<FEventSeedWarnings>();
    if (std::abs(chestPercent - std::round(chestPercent)) > kSeedPercentEpsilon) {
        if (warnings->nonWholeFraction != chestFraction) {
            warnings->nonWholeFraction = chestFraction;
            LOG_WARN("event seed: recovery.event_chest_hp_cost_fraction {} x 100 is not a whole number; "
                     "the cache cost uses {}%.", chestFraction, chestCostPercent);
        }
    }
    const bool costsHp = chestFraction > 0.0;
    if (!costsHp) {
        if (warnings->zeroFraction != chestFraction) {
            warnings->zeroFraction = chestFraction;
            LOG_WARN("event seed: recovery.event_chest_hp_cost_fraction is 0 (or recovery is absent); the "
                     "cache choice has no HP cost.");
        }
    }

    FEventDef seed;
    seed.id = std::string(kLegacySeedEventId);
    seed.title = std::string(kSeedTitle);
    seed.body = std::string(kSeedBody);
    seed.tags = {std::string(kLegacySeedPool)};

    FEventChoice rest;
    rest.text = std::string(kSeedRestTextPrefix) + std::to_string(heal) + std::string(kSeedRestTextSuffix);
    rest.effects.push_back(gameplay::FEffectSpec{std::string(kEventEffectRestorePartyHp), heal});
    rest.resultText = std::string(kSeedRestResult);
    seed.choices.push_back(std::move(rest));

    FEventChoice cache;
    cache.text = std::string(kSeedCacheTextPrefix) + std::to_string(materials) + std::string(kSeedCacheTextSuffix);
    if (costsHp) cache.text += kSeedCacheCostSuffix;
    cache.effects.push_back(gameplay::FEffectSpec{std::string(kEventEffectGainMaterials), materials});
    if (costsHp) {
        cache.effects.push_back(
            gameplay::FEffectSpec{std::string(kEventEffectDamagePartyPct), chestCostPercent});
    }
    cache.resultText = std::string(kSeedCacheResult);
    seed.choices.push_back(std::move(cache));

    return std::vector<FEventDef>{std::move(seed)};
}

std::vector<FEventDef> EventCatalog(entt::registry& registry) {
    std::vector<FEventDef> catalog = BuildLegacySeedEvents(registry);
    if (const FEventTable* table = registry.ctx().find<FEventTable>()) {
        catalog.insert(catalog.end(), table->events.begin(), table->events.end());
    }
    return catalog;
}

std::optional<FEventDef> FindEventDef(entt::registry& registry, std::string_view id) {
    // One entry, never the whole catalog (WP-09h #8): the view calls this every frame, so copying the
    // authored table and rebuilding the seed for nothing is off the table. Nothing is cached either — the
    // seed is still derived from the current balance/recovery on every read, so a re-initialized dungeon is
    // reflected immediately (the one-canonical-source contract in BuildLegacySeedEvents).
    if (id == kLegacySeedEventId) {
        std::vector<FEventDef> seed = BuildLegacySeedEvents(registry);
        if (seed.empty()) return std::nullopt;  // the builder always returns one seed; defensive only
        return std::optional<FEventDef>{std::move(seed.front())};
    }
    if (const FEventTable* table = registry.ctx().find<FEventTable>()) {
        for (const FEventDef& event : table->events) {
            if (event.id == id) return std::optional<FEventDef>{event};
        }
    }
    return std::nullopt;
}

bool EventInPool(const FEventDef& event, std::string_view pool) {
    if (pool.empty()) return true;  // "" = every event
    return std::ranges::find(event.tags, pool) != event.tags.end();
}

bool ValidateEventContent(entt::registry& registry, const FAssetManager& assets) {
    bool valid = true;

    const FEventTable* table = registry.ctx().find<FEventTable>();
    const std::span<const FEventDef> authored =
        table != nullptr ? std::span<const FEventDef>{table->events} : std::span<const FEventDef>{};

    // ① Effect kinds: every authored kind must be registered with Run scope and accepted by its
    // registered validator. The seed's kinds are deliberately not checked here — the seed is built from
    // `data/dungeon balance`, which the boot has not loaded yet, and its kinds are the same constants
    // RegisterEventEffects installs.
    const FEffectRegistry* effects = registry.ctx().find<FEffectRegistry>();
    for (const FEventDef& event : authored) {
        for (std::size_t choiceIndex = 0; choiceIndex < event.choices.size(); ++choiceIndex) {
            for (const gameplay::FEffectSpec& spec : event.choices[choiceIndex].effects) {
                if (effects == nullptr) {
                    LOG_ERROR("event content: event '{}' choice {} uses effect kind '{}' but no effect "
                              "registry is installed.", event.id, choiceIndex, spec.kind);
                    valid = false;
                    continue;
                }
                const FEffectRegistry::FKind* kind = effects->find(spec.kind);
                if (kind == nullptr) {
                    LOG_ERROR("event content: event '{}' choice {} uses effect kind '{}', which is not "
                              "registered.", event.id, choiceIndex, spec.kind);
                    valid = false;
                    continue;
                }
                if (kind->scope != gameplay::FEffectScope::Run) {
                    LOG_ERROR("event content: event '{}' choice {} uses effect kind '{}', which is not a "
                              "Run-scope effect.", event.id, choiceIndex, spec.kind);
                    valid = false;
                    continue;
                }
                if (kind->validate && !kind->validate(spec)) {
                    LOG_ERROR("event content: event '{}' choice {} effect kind '{}' refused its "
                              "parameters.", event.id, choiceIndex, spec.kind);
                    valid = false;
                }
                if (const std::optional<std::string> shape =
                        gameplay::ValidateEffectSpec(spec, gameplay::FEffectScope::Run)) {
                    LOG_ERROR("event content: event '{}' choice {} effect kind '{}' is malformed: {}.",
                              event.id, choiceIndex, spec.kind, *shape);
                    valid = false;
                }
            }
        }
        // ② Gate dominance guard: at least one choice must stay open even for a fully qualified party.
        std::size_t gated = 0;
        for (const FEventChoice& choice : event.choices) {
            // WP-41: a pay_materials effect gates the choice too (the party may be unable to pay).
            const bool pays = std::ranges::any_of(choice.effects, [](const gameplay::FEffectSpec& effect) {
                return effect.kind == kEventEffectPayMaterials;
            });
            if (choice.check.has_value() || pays) ++gated;
        }
        if (gated + 1 > event.choices.size()) {
            LOG_ERROR("event content: event '{}' gates {} of {} choices; at least one choice must stay "
                      "ungated.", event.id, gated, event.choices.size());
            valid = false;
        }
    }

    // ③ Slot cross-check: read `data/dungeon` straight through the asset boundary (the ValidateSkillApCap
    // pattern) and look at the room slots only — this is not the sequence semantics. A content-only boot
    // without the document skips the checks; the dungeon loader owns its required shape.
    const std::optional<std::string> dungeonPath = assets.resolvePath("data/dungeon");
    if (!dungeonPath) {
        LOG_INFO("event content: 'data/dungeon' is not loaded; the room slot checks are skipped.");
        return valid;
    }
    const auto document = FJsonLoader::Load(*dungeonPath, kDungeonDocumentSchemaVersion);
    if (!document) return false;  // the reason is already logged
    const nlohmann::json& data = *document;

    const auto themesIt = data.find("themes");
    if (themesIt == data.end() || !themesIt->is_array()) {
        return valid;  // no slots to check; InitializeDungeon owns the document shape
    }
    for (const nlohmann::json& theme : *themesIt) {
        if (!theme.is_object()) continue;
        const auto dungeonsIt = theme.find("dungeons");
        if (dungeonsIt == theme.end() || !dungeonsIt->is_array()) continue;
        for (const nlohmann::json& dungeon : *dungeonsIt) {
            if (!dungeon.is_object()) continue;
            const auto roomsIt = dungeon.find("rooms");
            if (roomsIt == dungeon.end() || !roomsIt->is_array()) continue;
            for (const nlohmann::json& room : *roomsIt) {
                if (!room.is_object()) continue;
                const auto typeIt = room.find("type");
                if (typeIt == room.end() || !typeIt->is_string()) {
                    LOG_ERROR("event content: a dungeon room slot has no string 'type' (data/dungeon).");
                    valid = false;
                    continue;
                }
                const std::string type = typeIt->get<std::string>();
                if (!IsKnownRoomType(registry, type)) {
                    LOG_ERROR("event content: room slot type '{}' is neither 'battle' nor a registered "
                              "room type.", type);
                    valid = false;
                    continue;
                }
                if (const FRoomTypeDef* typeDef = FindRoomType(registry, type)) {
                    const auto slotIt = room.find(typeDef->requiredSlotKey);
                    if (slotIt == room.end() || !slotIt->is_string()) {
                        LOG_ERROR("event content: a room slot of type '{}' is missing the string '{}' "
                                  "field.", type, typeDef->requiredSlotKey);
                        valid = false;
                    }
                }
                // Every referenced pool must select at least one event (seed ∪ authored): a pool nobody
                // answers is a dead event room the pick would skip.
                const auto poolIt = room.find(std::string{kRoomSlotKeyEventPool});
                if (poolIt == room.end()) continue;
                if (!poolIt->is_string()) {
                    LOG_ERROR("event content: a room slot '{}' must be a string.", kRoomSlotKeyEventPool);
                    valid = false;
                    continue;
                }
                const std::string pool = poolIt->get<std::string>();
                if (!PoolHasAnyEvent(authored, pool)) {
                    LOG_ERROR("event content: event_pool '{}' matches no event (seed '{}' tag '{}' plus "
                              "{} authored event(s)).", pool, kLegacySeedEventId, kLegacySeedPool,
                              authored.size());
                    valid = false;
                }
            }
        }
    }
    return valid;
}

} // namespace game
