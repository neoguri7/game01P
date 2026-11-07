#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

#include <entt/entt.hpp>

namespace game {

/// Player preferences persisted in settings.json, independent of the run/meta saves (WP-24a). Only the
/// presentation layer consumes them (battle speed and skip today; WP-06 adds the AI think delay), so
/// rules code (gameplay/) must not include this header. Loaded once at boot from the title anchor;
/// afterwards every consumer reads the context value — no file IO in the frame loop.
struct FSettings {
    float animSpeed{1.0f};       // 1.0 | 2.0 (kAnimSpeedNormal / kAnimSpeedFast)
    bool skipAnimations{false};
};

/// The only accepted battle speeds. A file or API value outside the set becomes 1.0 with a warning.
inline constexpr float kAnimSpeedNormal = 1.0f;
inline constexpr float kAnimSpeedFast = 2.0f;

/// settings.json schema version. WP-24b extends the document; unknown fields survive a save.
inline constexpr int kSettingsSchemaVersion = 1;
/// The settings file name and the save-directory override key it honours (WP-00b isolation).
inline constexpr const char* kSettingsFileName = "settings.json";
inline constexpr const char* kSettingsSaveDirEnv = "GAME01P_SAVE_DIR";

/// Where LoadSettings resolved the file for this session — a sibling context value so SaveSettings can
/// never write a different path than the load resolved. A missing path means memory-only (the override
/// directory could not be made, or no storage exists): setters still change the running game, nothing
/// is written.
struct FSettingsLocation {
    std::optional<std::filesystem::path> path;
    bool saveWarned{false};  // "cannot persist" / write failure is logged once per resolution
};

/// The loaded settings, or the 1x / no-skip defaults when nothing was loaded (headless tests, forest).
[[nodiscard]] const FSettings& CurrentSettings(entt::registry& registry);

/// Boot-time load (the title anchor runs it once from InitializeDungeonUi). Resolves the path, reads the
/// file, stores FSettings + FSettingsLocation. A missing file is a normal first boot; a corrupt or
/// invalid one warns and falls back to defaults — never a boot failure.
void LoadSettings(entt::registry& registry);

/// Writes the current settings immediately (read-modify-write, so unknown fields survive). A memory-only
/// session logs one warning and does nothing. Called by every setter.
void SaveSettings(entt::registry& registry);

void SetAnimSpeed(entt::registry& registry, float speed);
void ToggleAnimSpeed(entt::registry& registry);
void SetSkipAnimations(entt::registry& registry, bool skip);

/// The preference-directory fallback (SDL_GetPrefPath in the game; a probe-counting stub in tests). The
/// resolver's contract: with an override set this is never called — a failed override never falls back
/// to the user's real settings file.
using FPrefDirProvider = std::function<std::optional<std::filesystem::path>()>;

/// Resolves where settings.json lives: ① the `GAME01P_SAVE_DIR` directory (created on demand; on failure
/// nullopt — the preference directory is not a fallback), ② beside the run save, ③ the preference
/// directory. nullopt means memory-only.
[[nodiscard]] std::optional<std::filesystem::path> ResolveSettingsPath(
    const std::optional<std::filesystem::path>& overrideDir, std::string_view runSavePath,
    const FPrefDirProvider& prefPath);

} // namespace game
