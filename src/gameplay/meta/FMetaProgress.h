#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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
/// shipped meta content (roster/unlocks through the FAssetManager boundary — a missing or invalid file is
/// a boot failure), resolves storage, loads the save (corrupt = backup + defaults; a future version = the
/// session runs read-only), imports the checkpoint once when the save is absent, and registers the run
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

} // namespace game
