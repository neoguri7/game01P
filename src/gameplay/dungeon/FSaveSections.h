#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

namespace game {

class FAssetManager;

/// One module's save section: state that lives in `run.ext.<id>` instead of the core schema, so a module
/// (rewards, run flags, the run log, ...) owns its own format and version — the core never names a module.
///
/// `pack` reads the module's state out of the registry; `unpack` receives `{}` when the section is absent
/// (the module's default). Registration happens through RegisterSaveSection at the composition root; there
/// is no static self-registration (the runtime is a static library and drops unreferenced objects).
struct FSaveSectionSerializer {
    std::string id;  // the `run.ext.<id>` key: "offers", "flags", "log", ...
    int version{1};  // the section's own version; the module owns its migration
    std::function<nlohmann::json(entt::registry&)> pack;
    std::function<void(entt::registry&, const nlohmann::json&)> unpack;  // an empty object = the module's default
};

/// A section written by another build: kept verbatim (deep copy) and re-emitted on the next save of the
/// same run, so an older build never destroys a newer module's state.
struct FPreservedSection {
    std::string id;
    std::uint32_t runSeed{0};  // the run it was loaded from; a save of another run drops it
    nlohmann::json raw;
};

/// The section registry, owned by the registry context like every other service.
struct FSaveSectionRegistry {
    std::vector<FSaveSectionSerializer> serializers;
    std::vector<FPreservedSection> preserved;
};

/// Registers one module serializer, creating the registry when it is absent. An empty id, a missing
/// pack/unpack, or a duplicate id is logged and ignored (first registration wins).
void RegisterSaveSection(entt::registry& registry, FSaveSectionSerializer serializer);

/// Seeds the empty section registry; save-section modules (WP-17/18/19a) register their serializer under
/// their own anchor in FGameplayServices::Initialize.
void RegisterCoreSaveSections(entt::registry& registry);

/// Drops the preserved unknown sections; a new run starts with a clean slate (WP-02a calls this when a
/// run begins). A no-op when the registry is absent.
void ClearPreserved(entt::registry& registry);

/// Loads every registered section from a save document (v1 or v2) into the registry. Unknown sections are
/// preserved for the next save; a known section newer than its serializer, or a malformed document, fails
/// the whole load rather than silently losing module state.
///
/// Call contract (wired by WP-02a): after DungeonRunFromJson accepts the same document, UnpackSections must
/// succeed before the run is installed. Saving goes through PackAndWriteSave, and a new run calls
/// ClearPreserved.
[[nodiscard]] std::expected<void, std::string> UnpackSections(entt::registry& registry, const nlohmann::json& document);

/// Writes every registered section into `document.run.ext`, merges the preserved unknown sections, and
/// stamps `document.content_schema`. On any failure the document and the preserved list stay untouched.
[[nodiscard]] std::expected<void, std::string> PackSections(entt::registry& registry, nlohmann::json& document);

/// PackSections, then WriteSaveFile: a pack failure is logged and returned without touching the file.
[[nodiscard]] std::expected<void, std::string> PackAndWriteSave(
    entt::registry& registry, nlohmann::json document, const std::filesystem::path& path);

/// "units:1/skills:1/runes:1/statuses:1" — the loaded content tables' own schema versions, recorded in the
/// save's advisory `content_schema` field. A mismatch is warned about on load, never refused.
[[nodiscard]] std::string ContentSchemaSummary(const FAssetManager& assets);

} // namespace game
