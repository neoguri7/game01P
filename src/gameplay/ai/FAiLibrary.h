#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "core/AssetManager.h"
#include "gameplay/ai/FAiNodeRegistry.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/ai/FBehaviorTree.h"
#include "gameplay/data/FContentRegistry.h"

namespace game::ai {

/// One authored behavior: a tree plus the judgement numbers it runs with. Units point at a profile by id
/// (`units.json` -> "ai"); several monsters can share one, and one profile is shared by all of them — the
/// tree is immutable, only each actor's FBtMemory differs (§6.4.2).
struct FAiProfile {
    std::string id;
    std::shared_ptr<const FBehaviorTree> tree;
    FAiTuning tuning;
};

/// Every AI profile of the game, built once at boot from `assets/data/ai.json` (R22/R23). Lives in the
/// registry context next to FContentRegistry.
///
/// Document shape:
///   { "schema_version": 1,
///     "tuning":    { "<key>": <int>, ... },                       defaults for every profile
///     "fragments": [ { "id": "fragment.x", "node": <node> } ],    reusable subtrees, pulled in by Ref
///     "profiles":  [ { "id": "ai.x", "tuning": {...}, "tree": <node> } ] }
///   <node> = { "type": "<registered type>", <parameters>, "children": [ <node>, ... ] }
///   A node of type "Ref" with "id": "fragment.x" is replaced by a fresh copy of that fragment.
class FAiLibrary {
public:
    /// The profile every unit without an `ai` field runs. Required in the document.
    static constexpr std::string_view kDefaultProfileId = "ai.default";

    /// Loads and validates `data/ai`. Nullopt means the document is unusable; the reason is logged and the
    /// boot must stop (a broken tree is a content bug, not something to patch around mid-fight).
    [[nodiscard]] static std::optional<FAiLibrary> load(
        const FAssetManager& assets, const gameplay::FContentRegistry& content);

    /// Same validation for an in-memory document, with an extended node vocabulary. On failure the reason
    /// is written to `error`. This is the entry point for tests and tools.
    [[nodiscard]] static std::optional<FAiLibrary> fromDocument(
        const nlohmann::json& document,
        const gameplay::FContentRegistry& content,
        const FAiNodeRegistry& nodes,
        std::string& error);

    [[nodiscard]] const FAiProfile* find(std::string_view id) const;
    [[nodiscard]] const FAiProfile& fallback() const { return profiles_.front(); }
    [[nodiscard]] const std::vector<FAiProfile>& profiles() const { return profiles_; }

private:
    std::vector<FAiProfile> profiles_; // profiles_.front() is always ai.default
};

} // namespace game::ai
