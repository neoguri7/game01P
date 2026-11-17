#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace game::gameplay {

/// The one trigger kind (WP-25): the engine fires when the TARGET already carries the status BEFORE the
/// hit lands. Pre-hit reading is the whole design (W6 review ★5–8): judging after the application would
/// make `rune.fire` satisfy its own combo on the very first hit, so the condition would carry no
/// information beyond `requires_runes`. The structure is setup → payoff.
inline constexpr std::string_view kEngineTriggerStatusOnTarget = "status_on_target";

/// Runeword effect kinds (WP-45). An engine with an empty effectKind stays journal-only.
inline constexpr std::string_view kEngineEffectDetonate = "detonate";
inline constexpr std::string_view kEngineEffectSpread = "spread";
inline constexpr std::string_view kEngineEffectArc = "arc";
inline constexpr std::string_view kEngineEffectCarry = "carry";

/// One engine as authored in `assets/data/engine_ignition.json` (WP-25): a rune pair plus the pre-hit
/// status that proves the pair is "on". The system only interprets — adding an archetype is data, not code.
struct FEngineContent {
    std::string id;
    std::string name;                       // display label for future UI; the journal line uses `log`
    std::vector<std::string> requiresRunes; // every id must be socketed in the acting slot
    std::string triggerKind;                // kEngineTriggerStatusOnTarget (validated at boot)
    std::string triggerStatusId;            // the status the TARGET must already carry
    int minMagnitude{0};                    // 0 = presence alone (freeze is authored magnitude 0)
    std::string log;                        // journal line body (English — the HUD font has no Hangul)
    std::string text;         // player-facing effect sentence (English); required when effectKind is set
    std::string effectKind;   // "" = no battle effect (journal only), else one of the four constants
    int effectPercent{0};     // detonate, arc: percent of the base amount (authored; 0 when absent)
    int effectTurns{0};       // spread: duration of the spread status in turns (authored; 0 when absent)
};

/// Declared before kEngineFields, which points at it (constexpr address init, WP-26).
inline constexpr FContentField kEngineTriggerFields[] = {
    {"kind", EContentFieldType::Text, true},           // boundary: asset key
    {"status_id", EContentFieldType::Id, true},        // boundary: asset key
    {"min_magnitude", EContentFieldType::Number, false}, // boundary: asset key
};

inline constexpr std::size_t kEngineTriggerFieldKind = 0;
inline constexpr std::size_t kEngineTriggerFieldStatusId = 1;
inline constexpr std::size_t kEngineTriggerFieldMinMagnitude = 2;

static_assert(std::size(kEngineTriggerFields) == kEngineTriggerFieldMinMagnitude + 1,
              "kEngineTriggerFields and the kEngineTriggerField* indices drifted apart");

inline constexpr FContentSchema kEngineTriggerSchema{"", "", "", 1, kEngineTriggerFields};

/// Declared before kEngineFields, which points at it (WP-45).
inline constexpr FContentField kEngineEffectFields[] = {
    {"kind", EContentFieldType::Text, true},        // boundary: asset key
    {"percent", EContentFieldType::Number, false},  // boundary: asset key
    {"turns", EContentFieldType::Number, false},    // boundary: asset key
};

inline constexpr std::size_t kEngineEffectFieldKind = 0;
inline constexpr std::size_t kEngineEffectFieldPercent = 1;
inline constexpr std::size_t kEngineEffectFieldTurns = 2;

static_assert(std::size(kEngineEffectFields) == kEngineEffectFieldTurns + 1,
              "kEngineEffectFields and the kEngineEffectField* indices drifted apart");

inline constexpr FContentSchema kEngineEffectSchema{"", "", "", 1, kEngineEffectFields};

/// Field order IS the projection order FContentLoader uses (R22).
/// `requires_runes`/`trigger`/`log` are required: an engine without a rune pair, without a trigger or
/// without a journal line has no meaning the system could honor, so the loader rejects a missing key by
/// name (R19). `name` (display label, future UI) and the trigger's `min_magnitude` (0 = presence) are
/// optional. The empty-collection/empty-string checks stay in FContentRegistry's validation: the loader
/// checks presence and type, not vocabulary.
/// `invariant:` this array, the kEngineField* indices below, decodeEngine and
/// assets/data/engine_ignition.json must keep the same order and the same keys.
inline constexpr FContentField kEngineFields[] = {
    {"id", EContentFieldType::Text, true},                                  // boundary: asset key
    {"name", EContentFieldType::Text, false},                               // boundary: asset key
    {"requires_runes", EContentFieldType::IdArray, true},                   // boundary: asset key
    {"trigger", EContentFieldType::Object, true, &kEngineTriggerSchema},    // boundary: asset key
    {"log", EContentFieldType::Text, true},                                 // boundary: asset key
    {"text", EContentFieldType::Text, false},                               // boundary: asset key
    {"effect", EContentFieldType::Object, false, &kEngineEffectSchema},     // boundary: asset key
};

inline constexpr std::size_t kEngineFieldId = 0;
inline constexpr std::size_t kEngineFieldName = 1;
inline constexpr std::size_t kEngineFieldRequiresRunes = 2;
inline constexpr std::size_t kEngineFieldTrigger = 3;
inline constexpr std::size_t kEngineFieldLog = 4;
inline constexpr std::size_t kEngineFieldText = 5;
inline constexpr std::size_t kEngineFieldEffect = 6;

static_assert(std::size(kEngineFields) == kEngineFieldEffect + 1,
              "kEngineFields and the kEngineField* indices drifted apart");

/// Projects one validated row into an engine. Cannot fail (R22): the loader guarantees the required
/// fields (id/requires_runes/trigger/log) are present and typed before decode runs, so only `name` and
/// the trigger's `min_magnitude` can be absent. The trigger null check is defensive — an absent trigger
/// cannot reach this function anymore (required field), and if one ever did, the empty kind would be
/// rejected by the boot validation by name.
[[nodiscard]] FEngineContent decodeEngine(const FContentRow& row);

} // namespace game::gameplay
