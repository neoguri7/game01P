#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/meta/FMetaSave.h"

namespace game {

/// The store type lives in FMetaProgress.cpp; the API header only names it.
struct FMetaProgressStore;

/// WP-20: the permanent meta save file name. It lives beside the run save slot (checkpoint.json /
/// run_save.json / meta_save.json share one directory).
inline constexpr const char* kMetaSaveFileName = "meta_save.json";

/// The preference-directory fallback provider (SDL_GetPrefPath in the game; probe-counting stubs in
/// tests). Resolver contract: with an override set this is never called, and a failed override never falls
/// back to it — see ResolveMetaSavePath.
using FMetaPrefDirProvider = std::function<std::optional<std::filesystem::path>()>;

/// Boot (the composition root calls it inside the ember block, right after InitializeDungeon): loads the
/// shipped meta content (roster/unlocks/progression through the FAssetManager boundary — a missing or invalid
/// file is a boot failure), resolves storage, loads the save (corrupt = backup + defaults; a future version =
/// the session runs read-only), imports the checkpoint once when the save is absent, and registers the run
/// event listener exactly once per registry.
///
/// `path` (tests/tools) is the meta_save.json FILE path and outranks every other rule; empty resolves
/// through ResolveMetaSavePath. `prefPath` replaces SDL_GetPrefPath for isolation tests. Returns false
/// only for shipped-content failures; an unresolvable storage path is "meta off + one warning" and
/// returns true (a progress store must never stop the game from booting — save corruption and content
/// corruption are different grades).
[[nodiscard]] bool InitializeMetaProgress(
    entt::registry& registry, std::string path = {}, FMetaPrefDirProvider prefPath = {});

/// Erases the store. A no-op without one (the forest/verify shutdown paths). The listener registration
/// marker deliberately survives: a re-initialize does not register a second listener (the registration
/// rule is once per registry, Nystrom's lapsed-listener discipline).
void ShutdownMetaProgress(entt::registry& registry);

[[nodiscard]] int MetaSeals(entt::registry& registry);

/// True when this session must not read or write the meta save: a future-version file, a corrupt file
/// whose backup rename failed, no usable storage, or meta was never initialized. Grants are logged only.
[[nodiscard]] bool MetaDisabled(entt::registry& registry);

/// Direct grant/deduct API (WP-18's sealBonus and later purchasers). A disabled session logs the grant and
/// changes nothing; a zero amount is a no-op (no save).
void GrantSeals(entt::registry& registry, int amount, std::string_view reason);

/// Grants an unlock. The id must be in the shipped catalog (an unknown id is ignored with a warning — a
/// stored id outside unlocks.json would make the next load fail its cross-check). Idempotent.
void GrantMetaUnlock(entt::registry& registry, std::string_view unlockId);
[[nodiscard]] bool HasMetaUnlock(entt::registry& registry, std::string_view unlockId);

/// The title menu's read-only info line: "Seals N · Best dungeon M · Clears K"; "meta disabled" for a
/// read-only session; empty when meta was never initialized (the view then draws nothing).
[[nodiscard]] std::string MetaSummaryLine(entt::registry& registry);

/// Inspection API (tests and later consumers): the live document, nullptr when meta is not initialized.
[[nodiscard]] const FMetaSave* FindMetaSave(const entt::registry& registry);
/// The resolved meta_save.json path; empty when meta is off or not initialized.
[[nodiscard]] std::filesystem::path MetaSavePath(const entt::registry& registry);

/// ---------------------------------------------------------------------------------------------
/// [WP-19b] Run debrief accessors (D-20 ladder checkpoint / D-22 grand breakthroughs)
/// ---------------------------------------------------------------------------------------------

/// [WP-19b] D-20: ladder rank `rank`'s checkpoint (cleared dungeons). 0 when meta is off/absent, rank < 1 or the
/// rank has no row.
[[nodiscard]] int MetaLadderCheckpoint(entt::registry& registry, int rank = 1);
/// [WP-19b] D-22 achievements: the lifetime grand (final-boss) breakthrough count, FMetaStats::grand (repeats and
/// cycles included). 0 when meta is off/absent.
[[nodiscard]] int MetaGrandCount(entt::registry& registry);

/// Pure path resolver (the save-dir isolation contract, same shape as ResolveSettingsPath):
/// ① overrideDir set -> create it and use `<override>/meta_save.json`; a creation failure returns nullopt
///    and NEVER tries the preference directory (that is the user's real save location);
/// ② else a checkpoint path with a parent directory -> `<checkpoint dir>/meta_save.json` (in tests the
///    injected checkpoint makes meta live beside it);
/// ③ else the preference provider -> `<pref>/meta_save.json`;
/// nullopt means no usable storage (meta off).
[[nodiscard]] std::optional<std::filesystem::path> ResolveMetaSavePath(
    const std::optional<std::filesystem::path>& overrideDir, std::string_view checkpointPath,
    const FMetaPrefDirProvider& prefPath);

/// ---------------------------------------------------------------------------------------------
/// [WP-21b] L2 promotion (WP-21 §4) + the public catalog/gate view (WP-21 §1/§3)
/// ---------------------------------------------------------------------------------------------

/// [WP-21b] The HUD/battle contract when every promotion axis is off (Skill1..Skill4, 3-socket skill cap).
/// WP §4: active slots = kBaseActiveSkillSlots + (slotPromotion ? min(tier, slotCap - kBaseActiveSkillSlots) : 0).
inline constexpr int kBaseActiveSkillSlots = 4;

/// [WP-21b] `data/progression` (assets/data/progression.json), loaded and validated by InitializeMetaProgress
/// — a missing or invalid file is a boot failure. The struct is only the container: values come from the
/// data, never from code defaults.
struct FMetaProgression {
    int slotCap{};             // >= kBaseActiveSkillSlots
    bool slotPromotion{};      // ships false (active slots stay 4)
    bool socketPromotion{};    // ships false (rune socket bonus stays 0)
    int bossesPerPromotion{};  // >= 1
    int sealsPerOverflow{};    // >= 0
};

/// [WP-21b] The live progression table; nullptr when meta is not initialized.
[[nodiscard]] const FMetaProgression* FindMetaProgression(const entt::registry& registry);

/// [WP-21b] One member's promotion record; {} when meta is uninitialized or the member has none yet.
[[nodiscard]] FMetaPromotion MetaPromotionOf(const entt::registry& registry, std::string_view memberId);

/// [WP-21b] WP §4: kBaseActiveSkillSlots + (slotPromotion ? min(tier, slotCap - kBaseActiveSkillSlots) : 0);
/// kBaseActiveSkillSlots without meta/progression.
[[nodiscard]] int MemberActiveSkillSlots(const entt::registry& registry, std::string_view memberId);

/// [WP-21b] WP §4: socketPromotion ? min(tier, 1) : 0 — the follow-up HUD/socket WP applies it; nothing
/// consumes it in W9. Rune sockets themselves are 3 without it.
[[nodiscard]] int MemberRuneSocketBonus(const entt::registry& registry, std::string_view memberId);

/// [WP-21b] One unlock catalog row (`unlocks.json`). `id` is the content id (skill.x / rune.y / a roster id)
/// and is what meta_save.unlocks stores.
struct FMetaUnlockRow {
    std::string id;
    std::string kind;
    int cost{0};
    int sealGate{0};
};

/// [WP-21b] Public read-only view of the shipped catalogs. With a store: the store's catalog. Without one
/// (meta never initialized — tests, forest): parsed from the FAssetManager documents `data/unlocks` /
/// `data/roster` when they exist, else empty. Never throws.
[[nodiscard]] std::vector<FMetaUnlockRow> MetaUnlockCatalog(const entt::registry& registry);
[[nodiscard]] std::optional<FMetaUnlockRow> FindMetaUnlockRow(const entt::registry& registry, std::string_view id);
[[nodiscard]] std::vector<FMetaRosterRow> MetaRosterCatalog(const entt::registry& registry);

/// [WP-21b] The single gate predicate (skill pool, roster selection, rune offers): true when `contentId` has
/// an unlocks.json row and HasMetaUnlock(contentId) is false. Meta off/uninitialized = nothing unlocked =
/// gated rows hidden.
[[nodiscard]] bool IsUnlockGated(entt::registry& registry, std::string_view contentId);

/// ---------------------------------------------------------------------------------------------
/// [WP-21c] Unlocks tab purchase (WP-21 §1/§3): offer evaluation + the single purchase path
/// ---------------------------------------------------------------------------------------------

/// [WP-21c] The session state of one unlock catalog row (the Unlocks tab's status column). Purchasable means
/// the shop would accept the purchase — PurchaseMetaUnlock performs exactly that evaluation.
enum class EUnlockStatus { Purchasable, Owned, InsufficientSeals, BelowSealGate, Unavailable, MetaDisabled };

/// [WP-21c] One evaluated shop row: the catalog row (filled best-effort so a read-only session still lists
/// id/kind/cost) plus the session state; `reason` is the English UI text, numbers filled in.
struct FUnlockOffer {
    FMetaUnlockRow row;
    EUnlockStatus status{EUnlockStatus::MetaDisabled};
    std::string reason;
};

/// [WP-21c] Lifetime EARNED seals (R6 P8 ②): the sum of the milestone ledger — every ladder rank's `granted`
/// key ending in `#clear` adds kClearSeals, `#boss` adds kBossSeals (the same named constants the grant code
/// uses; no new save field). Balance spending never lowers it; L2 overflow seals and any future direct grants
/// are balance only, not lifetime-earned. 0 without a store or when meta is disabled.
[[nodiscard]] int LifetimeSealsEarned(const entt::registry& registry);

/// [WP-21c] The single evaluation order: no store or disabled -> MetaDisabled; unknown id -> Unavailable;
/// owned -> Owned; kind=roster whose roster target is missing or a `locked: true` stub -> Unavailable
/// (buying it would change nothing — the no-op purchase is forbidden); LifetimeSealsEarned < seal_gate ->
/// BelowSealGate; MetaSeals < cost -> InsufficientSeals; else Purchasable.
[[nodiscard]] FUnlockOffer EvaluateUnlockOffer(entt::registry& registry, std::string_view unlockId);

/// [WP-21c] Every catalog row evaluated, in catalog order (the Unlocks tab's list).
[[nodiscard]] std::vector<FUnlockOffer> BuildUnlockOffers(entt::registry& registry);

/// [WP-21c] The one purchase path: evaluate; only Purchasable proceeds — `save.seals -= cost` and the id
/// append land in ONE store mutation, then ONE SaveMetaProgress (a failed save keeps the in-memory purchase,
/// the same policy as the other grants). Returns the evaluated status (Purchasable = bought).
EUnlockStatus PurchaseMetaUnlock(entt::registry& registry, std::string_view unlockId);

} // namespace game
