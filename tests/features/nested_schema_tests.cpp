// Nested-schema loader tests (WP-26). Covers Object/ObjectArray fields end to end: recursive
// validation, index-bearing error paths, defaults, the maxItems guard, and byte-identical flat
// messages (the regression gate for every existing content schema).
//
// Schemas and documents are inline — tests/fixtures stays frozen. Each document is written into a
// unique temp directory and loaded through the real FAssetManager + FContentLoader boundary, so the
// test exercises the same code path the game boots through.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "core/Logger.h"
#include "core/data/FContentLoader.h"

namespace {

using game::EContentFieldType;
using game::FAssetManager;
using game::FContentField;
using game::FContentLoader;
using game::FContentRow;
using game::FContentSchema;

// ------------------------------------------------------------------------------------------------
// Schemas. A sub-schema is declared before the field array that points at it (constexpr address init).
// ------------------------------------------------------------------------------------------------

/// Leaf effect: the innermost object of the shapes below (WP-04 `effect{kind,...}`).
inline constexpr FContentField kEffectFields[] = {
    {"kind", EContentFieldType::Text, true},
    {"power", EContentFieldType::Number, false},
    {"duration", EContentFieldType::Number, false},
};
inline constexpr FContentSchema kEffectSchema{"", "", "", 1, kEffectFields};
enum { kEffectKind, kEffectPower, kEffectDuration };

/// WP-09 `check{stat, difficulty}`.
inline constexpr FContentField kCheckFields[] = {
    {"stat", EContentFieldType::Id, true},
    {"difficulty", EContentFieldType::Number, true},
};
inline constexpr FContentSchema kCheckSchema{"", "", "", 1, kCheckFields};
enum { kCheckStat, kCheckDifficulty };

/// WP-09 `choices[]{label, check{}, on_success[], on_failure[]}` — object arrays inside an array.
inline constexpr FContentField kChoiceFields[] = {
    {"label", EContentFieldType::Text, true},
    {"check", EContentFieldType::Object, true, &kCheckSchema},
    {"on_success", EContentFieldType::ObjectArray, false, &kEffectSchema},
    {"on_failure", EContentFieldType::ObjectArray, false, &kEffectSchema},
};
inline constexpr FContentSchema kChoiceSchema{"", "", "", 1, kChoiceFields};
enum { kChoiceLabel, kChoiceCheck, kChoiceOnSuccess, kChoiceOnFailure };

/// WP-04 `hooks[]{event, effect{}, once_per_turn?}` — an object inside an array element.
inline constexpr FContentField kHookFields[] = {
    {"event", EContentFieldType::Id, true},
    {"effect", EContentFieldType::Object, true, &kEffectSchema},
    {"once_per_turn", EContentFieldType::Boolean, false},
};
inline constexpr FContentSchema kHookSchema{"", "", "", 1, kHookFields};
enum { kHookEvent, kHookEffect, kHookOncePerTurn };

/// WP-09 event: `events[]{event_type, choices[]}` — three levels together with choice.check.
inline constexpr FContentField kEventFields[] = {
    {"event_type", EContentFieldType::Text, true},
    {"choices", EContentFieldType::ObjectArray, true, &kChoiceSchema},
};
inline constexpr FContentSchema kEventSchema{"", "", "", 1, kEventFields};
enum { kEventType, kEventChoices };

/// An optional `Object` whose child schema itself has an optional `Object` (recursive defaulting).
inline constexpr FContentField kMetaFields[] = {
    {"display_name", EContentFieldType::Text, true},
    {"default_effect", EContentFieldType::Object, false, &kEffectSchema},
};
inline constexpr FContentSchema kMetaSchema{"", "", "", 1, kMetaFields};
enum { kMetaDisplayName, kMetaDefaultEffect };

/// WP-18 `modifiers[]{stat, value}` and `difficulty[]{rank, modifiers[]}`.
inline constexpr FContentField kModifierFields[] = {
    {"stat", EContentFieldType::Id, true},
    {"value", EContentFieldType::Number, true},
};
inline constexpr FContentSchema kModifierSchema{"", "", "", 1, kModifierFields};
enum { kModifierStat, kModifierValue };

inline constexpr FContentField kDifficultyFields[] = {
    {"rank", EContentFieldType::Number, true},
    {"modifiers", EContentFieldType::ObjectArray, false, &kModifierSchema},
};
inline constexpr FContentSchema kDifficultySchema{"", "", "", 1, kDifficultyFields};
enum { kDifficultyRank, kDifficultyModifiers };

/// The top-level schema under test: every nested shape in one document (asset id `data/nested`).
inline constexpr FContentField kNestedFields[] = {
    {"id", EContentFieldType::Text, true},
    {"meta", EContentFieldType::Object, false, &kMetaSchema},
    {"hooks", EContentFieldType::ObjectArray, false, &kHookSchema},
    {"events", EContentFieldType::ObjectArray, false, &kEventSchema},
    {"difficulty", EContentFieldType::ObjectArray, false, &kDifficultySchema},
};
inline constexpr FContentSchema kNestedSchema{"data/nested", "skills", "id", 1, kNestedFields};
enum { kNestedId, kNestedMeta, kNestedHooks, kNestedEvents, kNestedDifficulty };

/// Same shape as kNestedSchema with a per-field cap (maxItems override).
inline constexpr FContentField kCappedFields[] = {
    {"id", EContentFieldType::Text, true},
    {"hooks", EContentFieldType::ObjectArray, false, &kHookSchema, 2},
};
inline constexpr FContentSchema kCappedSchema{"data/nested", "skills", "id", 1, kCappedFields};

/// Schema-authoring bugs: object fields that declare no sub-schema (must fail, never crash).
inline constexpr FContentField kNullObjectFields[] = {
    {"id", EContentFieldType::Text, true},
    {"meta", EContentFieldType::Object, false, nullptr},
};
inline constexpr FContentSchema kNullObjectSchema{"data/nested", "skills", "id", 1, kNullObjectFields};

inline constexpr FContentField kNullArrayFields[] = {
    {"id", EContentFieldType::Text, true},
    {"hooks", EContentFieldType::ObjectArray, false, nullptr},
};
inline constexpr FContentSchema kNullArraySchema{"data/nested", "skills", "id", 1, kNullArrayFields};

/// Flat schema: proves the pre-nesting message format is byte-invariant (the regression gate).
inline constexpr FContentField kFlatFields[] = {
    {"id", EContentFieldType::Text, true},
    {"display_name", EContentFieldType::Text, true},
    {"power", EContentFieldType::Number, false},
    {"tags", EContentFieldType::IdArray, false},
};
inline constexpr FContentSchema kFlatSchema{"data/nested", "skills", "id", 1, kFlatFields};
enum { kFlatId, kFlatDisplayName, kFlatPower, kFlatTags };

// Field order IS enum order; the static_asserts fixate the invariant at compile time (WP-26 §2).
static_assert(std::size(kEffectFields) == kEffectDuration + 1, "kEffectFields and the kEffect* indices drifted apart");
static_assert(std::size(kChoiceFields) == kChoiceOnFailure + 1, "kChoiceFields and the kChoice* indices drifted apart");
static_assert(std::size(kHookFields) == kHookOncePerTurn + 1, "kHookFields and the kHook* indices drifted apart");
static_assert(std::size(kNestedFields) == kNestedDifficulty + 1, "kNestedFields and the kNested* indices drifted apart");
static_assert(std::size(kFlatFields) == kFlatTags + 1, "kFlatFields and the kFlat* indices drifted apart");

// ------------------------------------------------------------------------------------------------
// Harness: temp asset root, document writer/loader, log capture.
// ------------------------------------------------------------------------------------------------

/// A unique throwaway asset root with a data/ subdirectory, removed when it goes out of scope.
struct FTempRoot {
    std::filesystem::path root;

    explicit FTempRoot(std::string_view label) {
        static int counter = 0;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path()
            / ("game01p-wp26-" + std::string{label} + "-" + std::to_string(ticks) + "-" + std::to_string(counter++));
        std::error_code error;
        std::filesystem::remove_all(root, error);
        CHECK(std::filesystem::create_directories(root / "data"));
    }

    ~FTempRoot() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    FTempRoot(const FTempRoot&) = delete;
    FTempRoot& operator=(const FTempRoot&) = delete;
};

/// Writes `document` as <root>/data/nested.json and loads it through a fresh manager, so every call
/// sees exactly the document it just wrote (the manager cache is per instance, like one boot).
[[nodiscard]] std::optional<std::vector<FContentRow>> LoadDocument(const std::filesystem::path& root,
                                                                   const FContentSchema& schema, const std::string& document) {
    {
        std::ofstream file(root / "data" / "nested.json", std::ios::binary | std::ios::trunc);
        file << document;
        file.flush();
        CHECK(file.good());
    } // closed before the manager opens it for reading
    const FAssetManager assets({root.string()});
    return FContentLoader::load(assets, schema);
}

/// Captures what the loader logs while alive. spdlog's Windows EOL is "\r\n", so assertions compare
/// complete lines through LogHasLine instead of raw substrings.
struct FLogCapture {
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger;

    FLogCapture() {
        logger = std::make_shared<spdlog::logger>("wp26-test-capture", std::make_shared<spdlog::sinks::ostream_sink_mt>(stream));
        logger->set_pattern("%v");
        logger->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger);
    }

    ~FLogCapture() { SetCurrentGameLogger(nullptr); }

    [[nodiscard]] std::string text() const { return stream.str(); }
};

/// Splits a captured log into lines with the trailing '\r' removed (spdlog writes "\r\n" on Windows).
[[nodiscard]] std::vector<std::string> LogLines(const std::string& log) {
    std::vector<std::string> lines;
    std::istringstream stream{log};
    std::string current;
    while (std::getline(stream, current)) {
        if (!current.empty() && current.back() == '\r') {
            current.pop_back();
        }
        lines.push_back(std::move(current));
    }
    return lines;
}

/// True when `line` appears as a complete log line (message byte-for-byte, eol-insensitive).
[[nodiscard]] bool LogHasLine(const std::string& log, std::string_view line) {
    for (const std::string& current : LogLines(log)) {
        if (current == line) {
            return true;
        }
    }
    return false;
}

/// Number of captured lines starting with `prefix` — pins "exactly one log line per failure".
[[nodiscard]] std::size_t CountLogLinesStartingWith(const std::string& log, std::string_view prefix) {
    std::size_t count = 0;
    for (const std::string& line : LogLines(log)) {
        if (line.starts_with(prefix)) {
            ++count;
        }
    }
    return count;
}

/// Loads `document` expecting the loader to reject it with `expectedLine` as a complete log line, and
/// with exactly one loader failure line in total: nested failures log only at the deepest point, so a
/// caller that logged again as it unwound would double-report and fail this check. (The FAssetManager
/// success line `asset '...' loaded from` does not start with the `content asset ` prefix.)
void ExpectRejected(const std::filesystem::path& root, const FContentSchema& schema, const std::string& document,
                    std::string_view expectedLine) {
    FLogCapture log;
    const std::optional<std::vector<FContentRow>> rows = LoadDocument(root, schema, document);
    CHECK(!rows.has_value());
    const std::string captured = log.text();
    CHECK(LogHasLine(captured, expectedLine));
    CHECK(CountLogLinesStartingWith(captured, "content asset ") == 1);
}

/// Loads `document` expecting success; returns the rows for the test body to inspect.
[[nodiscard]] std::vector<FContentRow> LoadAccepted(const std::filesystem::path& root, const FContentSchema& schema,
                                                    const std::string& document) {
    const std::optional<std::vector<FContentRow>> rows = LoadDocument(root, schema, document);
    CHECK(rows.has_value());
    return *rows;
}

/// A `hooks` array with `count` valid elements, for the round-trip and maxItems tests.
[[nodiscard]] std::string HooksDocument(std::size_t count) {
    nlohmann::json hooks = nlohmann::json::array();
    for (std::size_t index = 0; index < count; ++index) {
        nlohmann::json hook;
        hook["event"] = "on_hit";
        hook["effect"] = {{"kind", "damage"}};
        hooks.push_back(std::move(hook));
    }
    nlohmann::json document;
    document["schema_version"] = 1;
    document["skills"] = nlohmann::json::array({nlohmann::json{{"id", "skill.strike"}, {"hooks", std::move(hooks)}}});
    return document.dump();
}

constexpr std::string_view kRoundTripDocument = R"({
  "schema_version": 1,
  "skills": [
    {
      "id": "skill.strike",
      "meta": {"display_name": "Strike"},
      "hooks": [
        {"event": "on_hit", "effect": {"kind": "damage", "power": 6}, "once_per_turn": true},
        {"event": "on_kill", "effect": {"kind": "buff", "power": 2, "duration": 3}}
      ],
      "events": [
        {
          "event_type": "shrine",
          "choices": [
            {
              "label": "Pray",
              "check": {"stat": "will", "difficulty": 12},
              "on_success": [{"kind": "heal", "power": 5}],
              "on_failure": [{"kind": "damage", "power": 3}, {"kind": "debuff", "duration": 2}]
            }
          ]
        }
      ],
      "difficulty": [
        {"rank": 3, "modifiers": [{"stat": "hp", "value": 0.25}]}
      ]
    }
  ]
})";

// ------------------------------------------------------------------------------------------------
// Tests.
// ------------------------------------------------------------------------------------------------

/// 1-level (array elements), 2-level (WP-04 hooks[].effect{}, WP-18 difficulty[].modifiers[]) and
/// 3-level (WP-09 events[].choices[].check{}/on_success[]/on_failure[]) shapes round-trip.
void ObjectRoundTrip() {
    const FTempRoot temp{"roundtrip"};
    const std::vector<FContentRow> rows = LoadAccepted(temp.root, kNestedSchema, std::string{kRoundTripDocument});
    CHECK(rows.size() == 1);
    const FContentRow& row = rows.front();

    // WP-04 shape: hooks[]{event, effect{...}}.
    const std::span<const FContentRow> hooks = row.objectArray(kNestedHooks);
    CHECK(hooks.size() == 2);
    CHECK(hooks[0].text(kHookEvent) == "on_hit");
    CHECK(hooks[0].boolean(kHookOncePerTurn, false));
    const FContentRow* hitEffect = hooks[0].object(kHookEffect);
    CHECK(hitEffect != nullptr);
    CHECK(hitEffect->values.size() == std::size(kEffectFields));
    CHECK(hitEffect->text(kEffectKind) == "damage");
    CHECK(hitEffect->number(kEffectPower, -1.0) == 6.0);
    CHECK(hitEffect->number(kEffectDuration, -1.0) == 0.0); // absent optional inside a nested row -> dense default

    CHECK(hooks[1].text(kHookEvent) == "on_kill");
    CHECK(hooks[1].boolean(kHookOncePerTurn, true) == false); // missing optional defaults to false, not the fallback
    const FContentRow* killEffect = hooks[1].object(kHookEffect);
    CHECK(killEffect != nullptr);
    CHECK(killEffect->text(kEffectKind) == "buff");
    CHECK(killEffect->number(kEffectDuration, -1.0) == 3.0);

    // WP-09 shape: events[].choices[].check{} / on_success[] / on_failure[].
    const std::span<const FContentRow> events = row.objectArray(kNestedEvents);
    CHECK(events.size() == 1);
    CHECK(events[0].text(kEventType) == "shrine");
    const std::span<const FContentRow> choices = events[0].objectArray(kEventChoices);
    CHECK(choices.size() == 1);
    CHECK(choices[0].text(kChoiceLabel) == "Pray");
    const FContentRow* check = choices[0].object(kChoiceCheck);
    CHECK(check != nullptr);
    CHECK(check->text(kCheckStat) == "will");
    CHECK(check->number(kCheckDifficulty, -1.0) == 12.0);
    const std::span<const FContentRow> success = choices[0].objectArray(kChoiceOnSuccess);
    CHECK(success.size() == 1);
    CHECK(success[0].text(kEffectKind) == "heal");
    CHECK(success[0].number(kEffectPower, -1.0) == 5.0);
    const std::span<const FContentRow> failure = choices[0].objectArray(kChoiceOnFailure);
    CHECK(failure.size() == 2);
    CHECK(failure[0].text(kEffectKind) == "damage");
    CHECK(failure[1].text(kEffectKind) == "debuff");
    CHECK(failure[1].number(kEffectDuration, -1.0) == 2.0);

    // WP-18 shape: difficulty[].modifiers[].
    const std::span<const FContentRow> difficulty = row.objectArray(kNestedDifficulty);
    CHECK(difficulty.size() == 1);
    CHECK(difficulty[0].number(kDifficultyRank, -1.0) == 3.0);
    const std::span<const FContentRow> modifiers = difficulty[0].objectArray(kDifficultyModifiers);
    CHECK(modifiers.size() == 1);
    CHECK(modifiers[0].text(kModifierStat) == "hp");
    CHECK(modifiers[0].number(kModifierValue, -1.0) == 0.25);

    // An Object field present projects exactly one row.
    const FContentRow* meta = row.object(kNestedMeta);
    CHECK(meta != nullptr);
    CHECK(meta->values.size() == std::size(kMetaFields));
    CHECK(meta->text(kMetaDisplayName) == "Strike");
}

/// Array elements are validated recursively: each element projects its own values, a non-object
/// element and an undeclared key inside an element both fail with the element index in the path.
void ObjectArrayValidated() {
    const FTempRoot temp{"array-validated"};
    const std::vector<FContentRow> rows = LoadAccepted(temp.root, kNestedSchema, HooksDocument(2));
    CHECK(rows.size() == 1);
    CHECK(rows.front().objectArray(kNestedHooks).size() == 2);

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit","effect":{"kind":"damage"}},42]}]})",
        "content asset 'data/nested': row 'skill.strike' field 'hooks[1]' must be a JSON object");

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit","effect":{"kind":"damage"},"bogus":1}]}]})",
        "content asset 'data/nested': row 'skill.strike' has unknown field 'hooks[0].bogus'");
}

/// Rule (2): an undeclared key inside a nested object is rejected at any depth, with the full path.
void UnknownKeyInsideNestedRejected() {
    const FTempRoot temp{"unknown-nested"};

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit","effect":{"kind":"damage","typo":1}}]}]})",
        "content asset 'data/nested': row 'skill.strike' has unknown field 'hooks[0].effect.typo'");

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","events":[{"event_type":"shrine","choices":[{"label":"Pray","check":{"stat":"will","difficulty":12,"bogus":true}}]}]}]})",
        "content asset 'data/nested': row 'skill.strike' has unknown field 'events[0].choices[0].check.bogus'");
}

/// maxItems: 64 elements load, 65 fail before any element is read; a per-field cap reports its limit.
void MaxItemsGuards() {
    const FTempRoot temp{"max-items"};
    const std::vector<FContentRow> rows = LoadAccepted(temp.root, kNestedSchema, HooksDocument(64));
    CHECK(rows.front().objectArray(kNestedHooks).size() == 64);

    ExpectRejected(temp.root, kNestedSchema, HooksDocument(65),
        "content asset 'data/nested': row 'skill.strike' field 'hooks' has 65 items, more than the 64 allowed");

    // The cap is checked before elements are read: an invalid element 0 stays unreported (the count
    // wins), proving the order.
    nlohmann::json oversized = nlohmann::json::parse(HooksDocument(65));
    oversized["skills"][0]["hooks"][0] = 42;
    ExpectRejected(temp.root, kNestedSchema, oversized.dump(),
        "content asset 'data/nested': row 'skill.strike' field 'hooks' has 65 items, more than the 64 allowed");

    // A custom smaller maxItems (2) rejects the third element with its own limit.
    ExpectRejected(temp.root, kCappedSchema, HooksDocument(3),
        "content asset 'data/nested': row 'skill.strike' field 'hooks' has 3 items, more than the 2 allowed");
}

/// Missing optional fields: Object -> one dense row of the child schema's defaults (recursively),
/// ObjectArray -> empty span; object()/objectArray() answer nullptr/empty on non-object slots.
void OptionalObjectDefaults() {
    const FTempRoot temp{"optional-defaults"};
    const std::vector<FContentRow> rows = LoadAccepted(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike"}]})");
    CHECK(rows.size() == 1);
    const FContentRow& row = rows.front();

    // Optional Object: a dense row with the child schema's defaults, recursively down to its own
    // optional Object.
    const FContentRow* meta = row.object(kNestedMeta);
    CHECK(meta != nullptr);
    CHECK(meta->values.size() == std::size(kMetaFields));
    CHECK(meta->text(kMetaDisplayName) == "");
    const FContentRow* defaultEffect = meta->object(kMetaDefaultEffect);
    CHECK(defaultEffect != nullptr);
    CHECK(defaultEffect->values.size() == std::size(kEffectFields));
    CHECK(defaultEffect->text(kEffectKind) == "");
    CHECK(defaultEffect->number(kEffectPower, -1.0) == 0.0);
    CHECK(defaultEffect->number(kEffectDuration, -1.0) == 0.0);

    // Optional ObjectArray: empty, and the accessors answer with the empty/null fallback.
    CHECK(row.objectArray(kNestedHooks).empty());
    CHECK(row.object(kNestedHooks) == nullptr);   // no rows to point at
    CHECK(row.object(kNestedId) == nullptr);      // flat slot is not an object
    CHECK(row.objectArray(kNestedId).empty());    // flat slot is not an object array
    CHECK(row.object(999) == nullptr);            // out-of-range slot
    CHECK(row.objectArray(999).empty());
}

/// A failure message keeps the old prefix and extends only the field slot to an indexed path.
void PathIncludesIndexAndKey() {
    const FTempRoot temp{"path-index"};
    // hooks[0] is valid, so the failure is unambiguously at index 1, inside effect.
    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit","effect":{"kind":"damage"}},{"event":"on_kill","effect":{"kind":42}}]}]})",
        "content asset 'data/nested': row 'skill.strike' field 'hooks[1].effect.kind' must be a text");
}

/// Missing required fields inside nested rows are reported with their full path.
void RequiredMissingInsideNested() {
    const FTempRoot temp{"required-nested"};

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit"}]}]})",
        "content asset 'data/nested': row 'skill.strike' is missing required field 'hooks[0].effect'");

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit","effect":{"power":1}}]}]})",
        "content asset 'data/nested': row 'skill.strike' is missing required field 'hooks[0].effect.kind'");

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","events":[{"event_type":"shrine","choices":[{"check":{"stat":"will","difficulty":12}}]}]}]})",
        "content asset 'data/nested': row 'skill.strike' is missing required field 'events[0].choices[0].label'");
}

/// Wrong shapes inside nested rows produce the legacy message with the nested path.
void TypeErrorInsideNested() {
    const FTempRoot temp{"type-nested"};

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":{"event":"on_hit"}}]})",
        "content asset 'data/nested': row 'skill.strike' field 'hooks' must be a JSON object array");

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit","effect":7}]}]})",
        "content asset 'data/nested': row 'skill.strike' field 'hooks[0].effect' must be a JSON object");

    ExpectRejected(temp.root, kNestedSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit","effect":{"kind":"damage"},"once_per_turn":"yes"}]}]})",
        "content asset 'data/nested': row 'skill.strike' field 'hooks[0].once_per_turn' must be a boolean");
}

/// A null sub-schema is a schema-authoring bug: reported on the read path and on the default path.
void NullSubSchemaRejected() {
    const FTempRoot temp{"null-schema"};

    // ObjectArray with data present.
    ExpectRejected(temp.root, kNullArraySchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","hooks":[{"event":"on_hit"}]}]})",
        "content asset 'data/nested': field 'hooks' declares an object without a schema");

    // ObjectArray absent: the default path must report the same way, not crash.
    ExpectRejected(temp.root, kNullArraySchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike"}]})",
        "content asset 'data/nested': field 'hooks' declares an object without a schema");

    // Optional Object absent: default recursion reports the null schema too.
    ExpectRejected(temp.root, kNullObjectSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike"}]})",
        "content asset 'data/nested': field 'meta' declares an object without a schema");
}

/// The field-order == enum-order invariant, again at runtime so a drift shows up as a named failure.
void SchemaFieldCountMatchesEnum() {
    CHECK(std::size(kEffectFields) == kEffectDuration + 1);
    CHECK(std::size(kCheckFields) == kCheckDifficulty + 1);
    CHECK(std::size(kChoiceFields) == kChoiceOnFailure + 1);
    CHECK(std::size(kHookFields) == kHookOncePerTurn + 1);
    CHECK(std::size(kEventFields) == kEventChoices + 1);
    CHECK(std::size(kMetaFields) == kMetaDefaultEffect + 1);
    CHECK(std::size(kModifierFields) == kModifierValue + 1);
    CHECK(std::size(kDifficultyFields) == kDifficultyModifiers + 1);
    CHECK(std::size(kNestedFields) == kNestedDifficulty + 1);
}

/// Flat schemas are untouched: values, the three legacy messages and the array message byte-for-byte.
void FlatSchemasUnchanged() {
    const FTempRoot temp{"flat"};

    const std::vector<FContentRow> rows = LoadAccepted(temp.root, kFlatSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","display_name":"Strike","power":6,"tags":["fire","melee"]}]})");
    CHECK(rows.size() == 1);
    CHECK(rows[0].text(kFlatId) == "skill.strike");
    CHECK(rows[0].text(kFlatDisplayName) == "Strike");
    CHECK(rows[0].number(kFlatPower, -1.0) == 6.0);
    CHECK(rows[0].textArray(kFlatTags) == (std::vector<std::string>{"fire", "melee"}));

    // Optional flat fields still default to the zero value inside a dense row.
    const std::vector<FContentRow> defaults = LoadAccepted(temp.root, kFlatSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","display_name":"Strike"}]})");
    CHECK(defaults[0].number(kFlatPower, -1.0) == 0.0);
    CHECK(defaults[0].textArray(kFlatTags).empty());

    // The three legacy messages, byte-for-byte (complete lines).
    ExpectRejected(temp.root, kFlatSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","display_name":"Strike","bogus":1}]})",
        "content asset 'data/nested': row 'skill.strike' has unknown field 'bogus'");
    ExpectRejected(temp.root, kFlatSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike"}]})",
        "content asset 'data/nested': row 'skill.strike' is missing required field 'display_name'");
    ExpectRejected(temp.root, kFlatSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","display_name":"Strike","power":"lots"}]})",
        "content asset 'data/nested': row 'skill.strike' field 'power' must be a number");

    // A flat array with a wrong element type keeps its long-standing index-free message.
    ExpectRejected(temp.root, kFlatSchema,
        R"({"schema_version":1,"skills":[{"id":"skill.strike","display_name":"Strike","tags":["fire",7]}]})",
        "content asset 'data/nested': row 'skill.strike' field 'tags' must be a id array");
}

void DefaultValueIsEmptyText() {
    const auto check = [](const game::FContentValue& value) {
        CHECK(value.type == EContentFieldType::Text);
        CHECK(value.asText() != nullptr && value.asText()->empty());
        CHECK(value.asRows() == nullptr);
        CHECK(value.asNumber() == nullptr);
    };
    game::FContentValue plain;
    check(plain);
    check(game::FContentValue{});
}

} // namespace

int main() {
    return game_test::RunTests("nested schema", {
        {"ObjectRoundTrip", ObjectRoundTrip},
        {"ObjectArrayValidated", ObjectArrayValidated},
        {"UnknownKeyInsideNestedRejected", UnknownKeyInsideNestedRejected},
        {"MaxItemsGuards", MaxItemsGuards},
        {"OptionalObjectDefaults", OptionalObjectDefaults},
        {"PathIncludesIndexAndKey", PathIncludesIndexAndKey},
        {"RequiredMissingInsideNested", RequiredMissingInsideNested},
        {"TypeErrorInsideNested", TypeErrorInsideNested},
        {"NullSubSchemaRejected", NullSubSchemaRejected},
        {"SchemaFieldCountMatchesEnum", SchemaFieldCountMatchesEnum},
        {"FlatSchemasUnchanged", FlatSchemasUnchanged},
        {"DefaultValueIsEmptyText", DefaultValueIsEmptyText},
    });
}
