#include "gameplay/dungeon/FSaveSections.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <string_view>
#include <utility>

#include "core/AssetManager.h"
#include "core/FAssetDigest.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonSave.h"

namespace game {
namespace {

constexpr const char* kSectionVersionKey = "section_version";

std::unexpected<std::string> Fail(std::string_view key, std::string_view problem) {
    return std::unexpected(std::string(key) + ": " + std::string(problem));
}

std::string SectionKey(std::string_view id) {
    return "run.ext." + std::string(id);
}

FSaveSectionRegistry& EnsureSectionRegistry(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<FSaveSectionRegistry>()) return *existing;
    return registry.ctx().emplace<FSaveSectionRegistry>();
}

const FSaveSectionSerializer* FindSerializer(const FSaveSectionRegistry& sections, std::string_view id) {
    for (const auto& serializer : sections.serializers) {
        if (serializer.id == id) return &serializer;
    }
    return nullptr;
}

/// The run identity every preserved section is tagged with. It must be usable — without it a preserved
/// subtree cannot be attributed to a run and could leak into another run's save.
std::expected<std::uint32_t, std::string> ReadRunSeed(const nlohmann::json& document) {
    const auto run = document.find("run");
    if (run == document.end() || !run->is_object()) return Fail("run", "must be an object");
    const auto seed = run->find("seed");
    if (seed == run->end() || !seed->is_number_integer() || seed->get<std::int64_t>() < 1 ||
        seed->get<std::int64_t>() > std::numeric_limits<std::uint32_t>::max()) {
        return Fail("run.seed", "must be an integer");
    }
    return static_cast<std::uint32_t>(seed->get<std::int64_t>());
}

/// Compares the document's advisory content fingerprint with the current build's. A mismatch only warns:
/// the save may still be playable, and content drift is a support signal, not a reason to drop a run.
void WarnOnContentSchema(const entt::registry& registry, const nlohmann::json& document) {
    const auto saved = document.find("content_schema");
    if (saved == document.end()) return;  // v1 saves carry none
    const auto* assets = registry.ctx().find<FAssetManager>();
    if (assets == nullptr) return;
    const std::string current = ContentSchemaSummary(*assets);
    if (!saved->is_string() || saved->get_ref<const std::string&>() != current) {
        LOG_WARN("Saved content_schema ({}) does not match this build ({}); continuing anyway.", saved->dump(), current);
    }
}

struct FSchemaSource {
    const char* asset;
    const char* label;
};

constexpr FSchemaSource kSchemaSources[] = {
    {"data/units", "units"},
    {"data/skills", "skills"},
    {"data/runes", "runes"},
    {"data/statuses", "statuses"},
};

} // namespace

void RegisterSaveSection(entt::registry& registry, FSaveSectionSerializer serializer) {
    if (serializer.id.empty() || !serializer.pack || !serializer.unpack) {
        LOG_ERROR("Save section registration ignored: a section needs an id, a pack and an unpack.");
        return;
    }
    auto& sections = EnsureSectionRegistry(registry);
    if (FindSerializer(sections, serializer.id) != nullptr) {
        LOG_ERROR("Save section '{}' is already registered; keeping the first registration.", serializer.id);
        return;
    }
    sections.serializers.push_back(std::move(serializer));
}

void RegisterCoreSaveSections(entt::registry& registry) {
    registry.ctx().emplace<FSaveSectionRegistry>();
}

void ClearPreserved(entt::registry& registry) {
    if (auto* sections = registry.ctx().find<FSaveSectionRegistry>()) {
        sections->preserved.clear();
    }
}

std::expected<void, std::string> UnpackSections(entt::registry& registry, const nlohmann::json& document) {
    auto* sections = registry.ctx().find<FSaveSectionRegistry>();
    if (sections == nullptr) return Fail("save sections", "are not initialized (wiring bug)");
    if (!document.is_object()) return Fail("save", "must be an object");
    const auto seed = ReadRunSeed(document);
    if (!seed) return std::unexpected(seed.error());
    const nlohmann::json& run = document.at("run");  // ReadRunSeed proved it exists and is an object

    const nlohmann::json* ext = nullptr;
    if (const auto found = run.find("ext"); found != run.end()) {
        if (!found->is_object()) return Fail("run.ext", "must be an object");
        ext = &*found;
    }
    WarnOnContentSchema(registry, document);

    // Validate everything, then apply: plan each unpack, refuse a malformed or too-new section, and
    // deep-copy the unknown subtrees. Nothing touches the registry or the preserved list until all pass.
    struct FPlannedSection {
        const FSaveSectionSerializer* serializer;
        const nlohmann::json* section;  // null = absent -> unpack gets {}
    };
    std::vector<FPlannedSection> planned;
    std::vector<FPreservedSection> preserved;
    for (const auto& serializer : sections->serializers) {
        const nlohmann::json* section = nullptr;
        if (ext != nullptr) {
            if (const auto found = ext->find(serializer.id); found != ext->end()) {
                if (!found->is_object()) return Fail(SectionKey(serializer.id), "must be an object");
                const auto version = found->find(kSectionVersionKey);
                if (version == found->end() || !version->is_number_integer() || version->get<std::int64_t>() < 1) {
                    return Fail(SectionKey(serializer.id), "section_version must be a positive integer");
                }
                if (version->get<std::int64_t>() > serializer.version) {
                    return Fail(SectionKey(serializer.id),
                                "section_version " + std::to_string(version->get<std::int64_t>()) +
                                    " is newer than this build supports (" + std::to_string(serializer.version) + ")");
                }
                section = &*found;
            }
        }
        planned.push_back(FPlannedSection{&serializer, section});
    }
    if (ext != nullptr) {
        for (const auto& [id, value] : ext->items()) {
            if (FindSerializer(*sections, id) != nullptr) continue;
            preserved.push_back(FPreservedSection{id, *seed, value});
        }
    }

    for (const auto& item : planned) {
        try {
            item.serializer->unpack(registry, item.section != nullptr ? *item.section : nlohmann::json::object());
        } catch (const std::exception& error) {
            return Fail(SectionKey(item.serializer->id), error.what());
        }
    }
    sections->preserved = std::move(preserved);
    return {};
}

std::expected<void, std::string> PackSections(entt::registry& registry, nlohmann::json& document) {
    auto* sections = registry.ctx().find<FSaveSectionRegistry>();
    if (sections == nullptr) return Fail("save sections", "are not initialized (wiring bug)");
    if (!document.is_object()) return Fail("save", "must be an object");
    const auto seed = ReadRunSeed(document);
    if (!seed) return std::unexpected(seed.error());

    // Build the whole replacement ext before touching the document: a failing pack must leave the document
    // and the preserved list exactly as they were (a partial save would look like lost module state).
    nlohmann::json ext = nlohmann::json::object();
    for (const auto& serializer : sections->serializers) {
        nlohmann::json packed;
        try {
            packed = serializer.pack(registry);
        } catch (const std::exception& error) {
            return Fail(SectionKey(serializer.id), error.what());
        }
        if (!packed.is_object()) return Fail(SectionKey(serializer.id), "pack must return an object");
        packed[kSectionVersionKey] = serializer.version;
        ext[serializer.id] = std::move(packed);
    }
    for (const auto& entry : sections->preserved) {
        if (entry.runSeed != *seed || FindSerializer(*sections, entry.id) != nullptr) continue;
        ext[entry.id] = entry.raw;  // other builds' sections ride along verbatim
    }

    if (const auto* assets = registry.ctx().find<FAssetManager>()) {
        document["content_schema"] = ContentSchemaSummary(*assets);
    }
    document["run"]["ext"] = std::move(ext);
    std::erase_if(sections->preserved, [seed = *seed](const FPreservedSection& entry) {
        return entry.runSeed != seed;  // another run's sections must never leak into this run's save
    });
    return {};
}

std::expected<void, std::string> PackAndWriteSave(
    entt::registry& registry, nlohmann::json document, const std::filesystem::path& path) {
    if (const auto packed = PackSections(registry, document); !packed) {
        LOG_ERROR("Expedition save aborted: {}", packed.error());
        return std::unexpected(packed.error());
    }
    std::string reason;
    if (!WriteSaveFile(path, document, reason)) {
        LOG_ERROR("{}", reason);
        return std::unexpected(reason);
    }
    return {};
}

std::string ContentSchemaSummary(const FAssetManager& assets) {
    std::string summary;
    for (const auto& source : kSchemaSources) {
        if (!summary.empty()) summary += '/';
        summary += source.label;
        summary += ':';
        std::string version = "?";
        if (const auto document = assets.digest(source.asset)) {
            const auto found = document->json().find("schema_version");
            if (found != document->json().end() && found->is_number_integer()) {
                version = std::to_string(found->get<std::int64_t>());
            }
        }
        summary += version;
    }
    return summary;
}

} // namespace game
