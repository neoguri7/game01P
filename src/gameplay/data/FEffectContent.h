#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game::gameplay {

/// Effect scope (WP-04 §2): Battle effects may only touch battle-scoped state, Run effects may only touch
/// run-scoped state (FDungeonRun). ValidateEffectContent refuses a hook whose kind was registered for the
/// other scope, and ApplyEffect refuses the mismatch at runtime too.
enum class FEffectScope { Battle, Run };

/// The typed hook events (WP-04 §3). Data authors one by name; the decoder cannot fail, so it keeps the
/// authored text in FHook::eventName and ValidateEffectContent rejects a name that does not parse — an
/// unknown event must fail the boot, never silently become OnHit.
enum class EHookEvent { OnHit, OnKill, OnDamageTaken, OnTurnStart, OnTurnEnd, OnRoomClear };

/// The authored name of one hook event ("on_hit", ...) — the vocabulary assets and logs speak.
[[nodiscard]] const char* HookEventName(EHookEvent event);

/// Maps an authored event name to its typed value. nullopt when the name is not part of the vocabulary.
[[nodiscard]] std::optional<EHookEvent> ParseHookEvent(std::string_view name);

/// One typed effect (WP-04 §1): the fixed `kEffectFields` projection. There is no dynamic JSON anywhere in
/// the rules runtime — the loader already rejected a wrong type, and kind-specific required parameters are
/// enforced by the registered per-kind validators (FEffectRegistry, FEffectHooks.h).
struct FEffectSpec {
    std::string kind;
    int amount{0};
    std::string statusId; // JSON key: status_id
    int duration{0};
    int magnitude{0};
    int distance{0};
    std::string from; // "caster" (push/pull)
    std::string stat;
    std::string pool;
    std::string member;
    std::string text;
};

/// One data-authored hook: which event it listens to, what it applies, whether it may fire once per turn,
/// where it came from and its identity. `eventName` keeps the authored text so validation can reject an
/// unknown name (decoders cannot fail). `source` and `id` are stamped by DecodeHooks.
struct FHook {
    EHookEvent event{EHookEvent::OnHit};
    std::string eventName;
    FEffectSpec effect;
    bool oncePerTurn{false};
    std::string source; // "skill:<id>" / "rune:<id>"
    std::string id;     // authored id, else "<source>#<index>"
};

/// Core Battle effect kind names (WP-04 owns these six). Named because both the registration root
/// (FEffectHooks.cpp) and the resolver (leech synthesis, FSkillResolver.cpp) speak them.
inline constexpr std::string_view kEffectKindDamageSelf = "damage_self";
inline constexpr std::string_view kEffectKindHealSelf = "heal_self";
inline constexpr std::string_view kEffectKindHealTarget = "heal_target";
inline constexpr std::string_view kEffectKindApplyStatusSelf = "apply_status_self";
inline constexpr std::string_view kEffectKindApplyStatusTarget = "apply_status_target";
inline constexpr std::string_view kEffectKindLog = "log";

/// Field order IS the projection order FContentLoader uses (R22), and the 11 keys are fixed (WP-04 §1):
/// a new key is one edit here plus one line in the owning WP. `stat`/`pool`/`member` are the later
/// Run-scope vocabulary (WP-09); this WP decodes them but registers no consumer.
inline constexpr FContentField kEffectFields[] = {
    {"kind", EContentFieldType::Text, true},            // boundary: asset key
    {"amount", EContentFieldType::Number, false},       // boundary: asset key
    {"status_id", EContentFieldType::Id, false},        // boundary: asset key
    {"duration", EContentFieldType::Number, false},     // boundary: asset key
    {"magnitude", EContentFieldType::Number, false},    // boundary: asset key
    {"distance", EContentFieldType::Number, false},     // boundary: asset key
    {"from", EContentFieldType::Text, false},           // boundary: asset key
    {"stat", EContentFieldType::Id, false},             // boundary: asset key
    {"pool", EContentFieldType::Id, false},             // boundary: asset key
    {"member", EContentFieldType::Id, false},           // boundary: asset key
    {"text", EContentFieldType::Text, false},           // boundary: asset key
};

inline constexpr std::size_t kEffectFieldKind = 0;
inline constexpr std::size_t kEffectFieldAmount = 1;
inline constexpr std::size_t kEffectFieldStatusId = 2;
inline constexpr std::size_t kEffectFieldDuration = 3;
inline constexpr std::size_t kEffectFieldMagnitude = 4;
inline constexpr std::size_t kEffectFieldDistance = 5;
inline constexpr std::size_t kEffectFieldFrom = 6;
inline constexpr std::size_t kEffectFieldStat = 7;
inline constexpr std::size_t kEffectFieldPool = 8;
inline constexpr std::size_t kEffectFieldMember = 9;
inline constexpr std::size_t kEffectFieldText = 10;

static_assert(std::size(kEffectFields) == kEffectFieldText + 1, "kEffectFields and the kEffectField* indices drifted apart");

/// Declared before kHookFields, which points at it (constexpr address init, WP-26).
inline constexpr FContentSchema kEffectSchema{"", "", "", 1, kEffectFields};

/// One authored hook (WP-04 §1): `{"event": "on_hit", "effect": {kind, ...}, "once_per_turn": false,
/// "id": "..."}`. The order IS the projection order; `hooks` is appended as the last field of the skill
/// and rune schemas, so every earlier index keeps its meaning.
inline constexpr FContentField kHookFields[] = {
    {"event", EContentFieldType::Id, true},                             // boundary: asset key
    {"effect", EContentFieldType::Object, true, &kEffectSchema},        // boundary: asset key
    {"once_per_turn", EContentFieldType::Boolean, false},               // boundary: asset key
    {"id", EContentFieldType::Id, false},                               // boundary: asset key
};

inline constexpr std::size_t kHookFieldEvent = 0;
inline constexpr std::size_t kHookFieldEffect = 1;
inline constexpr std::size_t kHookFieldOncePerTurn = 2;
inline constexpr std::size_t kHookFieldId = 3;

static_assert(std::size(kHookFields) == kHookFieldId + 1, "kHookFields and the kHookField* indices drifted apart");

inline constexpr FContentSchema kHookSchema{"", "", "", 1, kHookFields};

/// Decodes one authored `hooks` ObjectArray into typed hooks. `source` ("skill:<id>" / "rune:<id>") is
/// stamped on every hook; a hook without an authored id gets "<source>#<index>". Cannot fail: wrong types
/// were rejected at load, and an unknown event name is kept in eventName for ValidateEffectContent.
[[nodiscard]] std::vector<FHook> DecodeHooks(std::span<const FContentRow> rows, std::string_view source);

/// Registry-free shape check of one effect spec: the kind must be authored, and the Run-only vocabulary
/// (`pool`/`member`) may not appear in a Battle-scope spec. Registry-dependent checks — kind registered,
/// scope match, per-kind required parameters — run in ValidateEffectContent, which consults the
/// registered validators (the W2 redesign moved them out of this leaf function).
[[nodiscard]] std::optional<std::string> ValidateEffectSpec(const FEffectSpec& spec, FEffectScope scope);

} // namespace game::gameplay
