#include "presentation/FSettings.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonSave.h"

namespace game {
namespace {

constexpr FSettings kDefaultSettings{};

/// The GAME01P_SAVE_DIR override, or nullopt when unset or empty — the same boundary rule as
/// FDungeonRun.cpp's file-local SaveDirOverride (std::getenv, not SDL_getenv: SDL caches the environment
/// on first use, so a test that sets the variable later would not be seen).
std::optional<std::filesystem::path> SaveDirOverride() {
    const char* value = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996) // std::getenv: read-only, no buffer to overflow
#endif
    value = std::getenv(kSettingsSaveDirEnv);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
}

/// SDL_GetPrefPath("Game01P", "EmberDungeon") — the same directory the run save falls back to.
std::optional<std::filesystem::path> PreferenceDir() {
    char* path = SDL_GetPrefPath("Game01P", "EmberDungeon");
    if (path == nullptr) return std::nullopt;
    std::filesystem::path directory = path;
    SDL_free(path);
    return directory;
}

float SanitizeAnimSpeed(float speed) {
    if (speed == kAnimSpeedNormal || speed == kAnimSpeedFast) return speed;
    LOG_WARN("anim_speed must be {} or {}; got {} - using {}.", kAnimSpeedNormal, kAnimSpeedFast, speed,
             kAnimSpeedNormal);
    return kAnimSpeedNormal;
}

/// Snaps a stored/API text scale onto the accepted set: 1.15 does not survive float/JSON round trips
/// exactly, so a value within kTextScaleTolerance of a member becomes that member. Anything else warns and
/// falls back to 1.0 (the same shape as SanitizeAnimSpeed).
float SanitizeTextScale(float scale) {
    if (std::fabs(scale - kTextScaleNormal) <= kTextScaleTolerance) return kTextScaleNormal;
    if (std::fabs(scale - kTextScaleLarge) <= kTextScaleTolerance) return kTextScaleLarge;
    if (std::fabs(scale - kTextScaleLargest) <= kTextScaleTolerance) return kTextScaleLargest;
    LOG_WARN("text_scale must be {}, {} or {}; got {} - using {}.", kTextScaleNormal, kTextScaleLarge,
             kTextScaleLargest, scale, kTextScaleNormal);
    return kTextScaleNormal;
}

/// Applies the known keys of a parsed document onto `settings`. Bad individual values warn and keep
/// their default; an unknown schema version keeps all defaults. Never throws, never fails a boot.
void ApplySettingsDocument(const nlohmann::json& document, const std::filesystem::path& path, FSettings& settings) {
    if (!document.is_object()) {
        LOG_WARN("settings.json is not an object; using defaults: {}", path.string());
        return;
    }
    const auto version = document.find("schema_version");
    if (version != document.end() &&
        (!version->is_number_integer() || version->get<int>() != kSettingsSchemaVersion)) {
        LOG_WARN("settings.json declares an unknown schema version; using defaults: {}", path.string());
        return;
    }
    const auto speed = document.find("anim_speed");
    if (speed != document.end()) {
        if (speed->is_number()) settings.animSpeed = SanitizeAnimSpeed(speed->get<float>());
        else LOG_WARN("settings.json anim_speed must be a number; using {}.", kAnimSpeedNormal);
    }
    const auto skip = document.find("skip_animations");
    if (skip != document.end()) {
        if (skip->is_boolean()) settings.skipAnimations = skip->get<bool>();
        else LOG_WARN("settings.json skip_animations must be true or false; keeping the default.");
    }
    const auto scale = document.find("text_scale");
    if (scale != document.end()) {
        if (scale->is_number()) settings.textScale = SanitizeTextScale(scale->get<float>());
        else LOG_WARN("settings.json text_scale must be a number; using {}.", kTextScaleNormal);
    }
    const auto onboarding = document.find("onboarding_done");
    if (onboarding != document.end()) {
        if (onboarding->is_boolean()) settings.onboardingDone = onboarding->get<bool>();
        else LOG_WARN("settings.json onboarding_done must be true or false; keeping the default.");
    }
}

} // namespace

const FSettings& CurrentSettings(entt::registry& registry) {
    const auto* settings = registry.ctx().find<FSettings>();
    return settings != nullptr ? *settings : kDefaultSettings;
}

void LoadSettings(entt::registry& registry) {
    std::string_view runSavePath;
    if (const auto* save = registry.ctx().find<FDungeonSave>(); save != nullptr && !save->path.empty()) {
        runSavePath = save->path;
    }
    const std::optional<std::filesystem::path> path =
        ResolveSettingsPath(SaveDirOverride(), runSavePath, PreferenceDir);

    FSettings settings;
    if (path.has_value()) {
        if (const auto document = ReadSaveFile(*path)) {
            ApplySettingsDocument(*document, *path, settings);
        } else if (std::filesystem::exists(*path)) {
            LOG_WARN("settings.json is unreadable ({}) - using defaults.", path->string());
        }
    }
    registry.ctx().insert_or_assign<FSettings>(std::move(settings));
    registry.ctx().insert_or_assign<FSettingsLocation>(FSettingsLocation{path, false});
}

void SaveSettings(entt::registry& registry) {
    const auto* settings = registry.ctx().find<FSettings>();
    if (settings == nullptr) return;
    auto* location = registry.ctx().find<FSettingsLocation>();
    if (location == nullptr) location = &registry.ctx().emplace<FSettingsLocation>();
    if (!location->path.has_value()) {
        if (!location->saveWarned) {
            LOG_WARN("Settings cannot be saved: this session has no writable settings directory.");
            location->saveWarned = true;
        }
        return;
    }
    // Read-modify-write: the known keys are rewritten, unknown fields survive for later work packages.
    nlohmann::json document = nlohmann::json::object();
    if (const auto existing = ReadSaveFile(*location->path); existing.has_value() && existing->is_object()) {
        document = *existing;
    }
    document["schema_version"] = kSettingsSchemaVersion;
    document["anim_speed"] = settings->animSpeed;
    document["skip_animations"] = settings->skipAnimations;
    // The accepted set is 1.0 / 1.15 / 1.25. A float 1.15 prints as 1.149999976158142, so store the snapped
    // set member at two decimals: the file stays readable and hand-editable.
    document["text_scale"] = std::round(static_cast<double>(settings->textScale) * 100.0) / 100.0;
    document["onboarding_done"] = settings->onboardingDone;
    std::string error;
    if (!WriteSaveFile(*location->path, document, error)) {
        if (!location->saveWarned) {
            LOG_WARN("Settings could not be written: {}", error);
            location->saveWarned = true;
        }
        return;
    }
    location->saveWarned = false;
}

void SetAnimSpeed(entt::registry& registry, float speed) {
    if (!registry.ctx().contains<FSettings>()) registry.ctx().emplace<FSettings>();
    registry.ctx().get<FSettings>().animSpeed = SanitizeAnimSpeed(speed);
    SaveSettings(registry);
}

void ToggleAnimSpeed(entt::registry& registry) {
    SetAnimSpeed(registry, CurrentSettings(registry).animSpeed == kAnimSpeedFast ? kAnimSpeedNormal : kAnimSpeedFast);
}

void SetSkipAnimations(entt::registry& registry, bool skip) {
    if (!registry.ctx().contains<FSettings>()) registry.ctx().emplace<FSettings>();
    registry.ctx().get<FSettings>().skipAnimations = skip;
    SaveSettings(registry);
}

void SetTextScale(entt::registry& registry, float scale) {
    if (!registry.ctx().contains<FSettings>()) registry.ctx().emplace<FSettings>();
    registry.ctx().get<FSettings>().textScale = SanitizeTextScale(scale);
    SaveSettings(registry);
}

void CycleTextScale(entt::registry& registry) {
    const float current = CurrentSettings(registry).textScale;
    const float next = current == kTextScaleNormal ? kTextScaleLarge
        : current == kTextScaleLarge ? kTextScaleLargest : kTextScaleNormal;
    SetTextScale(registry, next);
}

void SetOnboardingDone(entt::registry& registry, bool done) {
    if (!registry.ctx().contains<FSettings>()) registry.ctx().emplace<FSettings>();
    registry.ctx().get<FSettings>().onboardingDone = done;
    SaveSettings(registry);
}

std::optional<std::filesystem::path> ResolveSettingsPath(const std::optional<std::filesystem::path>& overrideDir,
                                                         std::string_view runSavePath,
                                                         const FPrefDirProvider& prefPath) {
    if (overrideDir.has_value()) {
        std::error_code code;
        std::filesystem::create_directories(*overrideDir, code);
        if (code) {
            // Never fall back to the preference directory: that would be the user's real settings file.
            LOG_WARN("Settings override '{}' could not be created ({}); settings stay in memory.",
                     overrideDir->string(), code.message());
            return std::nullopt;
        }
        return *overrideDir / std::filesystem::path(kSettingsFileName);
    }
    if (!runSavePath.empty()) {
        return std::filesystem::path(runSavePath).parent_path() / std::filesystem::path(kSettingsFileName);
    }
    if (prefPath) {
        if (const std::optional<std::filesystem::path> directory = prefPath()) {
            return *directory / std::filesystem::path(kSettingsFileName);
        }
    }
    return std::nullopt;
}

} // namespace game
