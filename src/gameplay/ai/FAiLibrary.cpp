#include "gameplay/ai/FAiLibrary.h"

#include <algorithm>
#include <map>

#include <nlohmann/json.hpp>

#include "core/Logger.h"

namespace game::ai {
namespace {

constexpr int kMaxDepth = 24;
constexpr int kMaxNodesPerTree = 256;

using FError = std::string;

[[nodiscard]] std::optional<FError> CheckKeys(
    const nlohmann::json& object, std::initializer_list<std::string_view> allowed, const std::string& where) {
    for (const auto& [key, value] : object.items()) {
        if (std::ranges::find(allowed, std::string_view(key)) == allowed.end()) {
            return where + ": unknown key '" + key + "'";
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<FError> ApplyTuning(FAiTuning& tuning, const nlohmann::json& block, const std::string& where) {
    if (!block.is_object()) {
        return where + ": tuning must be an object";
    }
    for (const auto& [key, value] : block.items()) {
        const auto field = std::ranges::find(kAiTuningFields, std::string_view(key), &FAiTuningField::key);
        if (field == std::end(kAiTuningFields)) {
            return where + ": unknown tuning key '" + key + "'";
        }
        if (!value.is_number_integer()) {
            return where + ": tuning '" + key + "' must be a whole number";
        }
        const int number = value.get<int>();
        if (number < field->minimum || number > field->maximum) {
            return where + ": tuning '" + key + "' must be in [" + std::to_string(field->minimum) + ", " +
                   std::to_string(field->maximum) + "]";
        }
        tuning.*(field->member) = number;
    }
    return std::nullopt;
}

/// Turns one authored node into a node tree. Fragments are expanded here, so every Ref site gets its own
/// copy of the nodes (a node belongs to exactly one place in exactly one tree).
class FTreeBuilder {
public:
    FTreeBuilder(
        const FAiNodeRegistry& nodes,
        const gameplay::FContentRegistry& content,
        const std::map<std::string, const nlohmann::json*>& fragments)
        : nodes_(nodes), content_(content), fragments_(fragments) {}

    [[nodiscard]] FNodeBuild build(const nlohmann::json& node, const std::string& path) {
        nodeCount_ = 0;
        refStack_.clear();
        return buildNode(node, path, 0);
    }

private:
    [[nodiscard]] FNodeBuild buildNode(const nlohmann::json& node, const std::string& path, int depth) {
        if (depth > kMaxDepth) {
            return std::unexpected(path + ": tree is nested deeper than " + std::to_string(kMaxDepth));
        }
        if (++nodeCount_ > kMaxNodesPerTree) {
            return std::unexpected(path + ": tree has more than " + std::to_string(kMaxNodesPerTree) + " nodes");
        }
        if (!node.is_object()) {
            return std::unexpected(path + ": a node must be an object");
        }
        const auto typeIt = node.find("type");
        if (typeIt == node.end() || !typeIt->is_string()) {
            return std::unexpected(path + ": node has no 'type'");
        }
        const std::string type = typeIt->get<std::string>();
        const std::string here = path + "/" + type;

        if (type == "Ref") {
            return buildRef(node, here, depth);
        }
        const FAiNodeRegistry::FType* entry = nodes_.find(type);
        if (entry == nullptr) {
            return std::unexpected(here + ": unknown node type '" + type + "'");
        }

        std::vector<FBtNodePtr> children;
        if (const auto kids = node.find("children"); kids != node.end()) {
            if (!kids->is_array()) {
                return std::unexpected(here + ": 'children' must be an array");
            }
            for (std::size_t index = 0; index < kids->size(); ++index) {
                FNodeBuild child = buildNode((*kids)[index], here + "[" + std::to_string(index) + "]", depth + 1);
                if (!child) {
                    return child;
                }
                children.push_back(std::move(*child));
            }
        }
        const int count = static_cast<int>(children.size());
        if (count < entry->minChildren || count > entry->maxChildren) {
            return std::unexpected(here + ": expects " + std::to_string(entry->minChildren) + ".." +
                                   std::to_string(entry->maxChildren) + " children, has " + std::to_string(count));
        }

        FNodeParams params(node, here);
        FNodeSpec spec{params, children, content_};
        return entry->factory(spec);
    }

    [[nodiscard]] FNodeBuild buildRef(const nlohmann::json& node, const std::string& here, int depth) {
        FNodeParams params(node, here);
        const std::string id = params.text("id");
        if (const auto problem = params.finish()) {
            return std::unexpected(*problem);
        }
        if (node.contains("children")) {
            return std::unexpected(here + ": a Ref has no children");
        }
        const auto fragment = fragments_.find(id);
        if (fragment == fragments_.end()) {
            return std::unexpected(here + ": unknown fragment '" + id + "'");
        }
        if (std::ranges::find(refStack_, id) != refStack_.end()) {
            return std::unexpected(here + ": fragment '" + id + "' includes itself");
        }
        refStack_.push_back(id);
        FNodeBuild built = buildNode(*fragment->second, "fragment " + id, depth + 1);
        refStack_.pop_back();
        return built;
    }

    const FAiNodeRegistry& nodes_;
    const gameplay::FContentRegistry& content_;
    const std::map<std::string, const nlohmann::json*>& fragments_;
    std::vector<std::string> refStack_;
    int nodeCount_{0};
};

} // namespace

std::optional<FAiLibrary> FAiLibrary::fromDocument(
    const nlohmann::json& document,
    const gameplay::FContentRegistry& content,
    const FAiNodeRegistry& nodes,
    std::string& error) {
    const auto fail = [&error](std::string message) -> std::optional<FAiLibrary> {
        error = std::move(message);
        return std::nullopt;
    };
    if (!document.is_object()) {
        return fail("data/ai: the document must be an object");
    }
    if (const auto problem = CheckKeys(document, {"_comment", "schema_version", "tuning", "fragments", "profiles"}, "data/ai")) {
        return fail(*problem);
    }
    if (document.value("schema_version", 0) != 1) {
        return fail("data/ai: unsupported schema_version (expected 1)");
    }

    FAiTuning defaults;
    if (const auto it = document.find("tuning"); it != document.end()) {
        if (const auto problem = ApplyTuning(defaults, *it, "data/ai tuning")) {
            return fail(*problem);
        }
    }

    std::map<std::string, const nlohmann::json*> fragments;
    if (const auto it = document.find("fragments"); it != document.end()) {
        if (!it->is_array()) {
            return fail("data/ai: 'fragments' must be an array");
        }
        for (const nlohmann::json& fragment : *it) {
            if (!fragment.is_object() || !fragment.contains("id") || !fragment["id"].is_string() || !fragment.contains("node")) {
                return fail("data/ai: every fragment needs an 'id' and a 'node'");
            }
            if (const auto problem = CheckKeys(fragment, {"_comment", "id", "node"}, "data/ai fragment")) {
                return fail(*problem);
            }
            const std::string id = fragment["id"].get<std::string>();
            if (!fragments.emplace(id, &fragment["node"]).second) {
                return fail("data/ai: fragment '" + id + "' is defined twice");
            }
        }
    }

    const auto profilesIt = document.find("profiles");
    if (profilesIt == document.end() || !profilesIt->is_array() || profilesIt->empty()) {
        return fail("data/ai: 'profiles' must be a non-empty array");
    }

    FTreeBuilder builder(nodes, content, fragments);
    FAiLibrary library;
    for (const nlohmann::json& profile : *profilesIt) {
        if (!profile.is_object() || !profile.contains("id") || !profile["id"].is_string() || !profile.contains("tree")) {
            return fail("data/ai: every profile needs an 'id' and a 'tree'");
        }
        const std::string id = profile["id"].get<std::string>();
        if (const auto problem = CheckKeys(profile, {"_comment", "id", "tuning", "tree", "intent_mode"}, "data/ai profile " + id)) {
            return fail(*problem);
        }
        if (library.find(id) != nullptr) {
            return fail("data/ai: profile '" + id + "' is defined twice");
        }
        FAiTuning tuning = defaults;
        if (const auto it = profile.find("tuning"); it != profile.end()) {
            if (const auto problem = ApplyTuning(tuning, *it, "data/ai profile " + id + " tuning")) {
                return fail(*problem);
            }
        }
        // WP-11: an authored intent-mode override. "hybrid" is the explicit spelling of the default rule
        // (absent = same); anything else is a content bug and must stop the boot, like every other key.
        std::optional<EIntentMode> intentMode;
        if (const auto it = profile.find("intent_mode"); it != profile.end()) {
            if (!it->is_string()) {
                return fail("data/ai profile " + id + ": intent_mode must be a string");
            }
            const std::string mode = it->get<std::string>();
            if (mode == "hybrid") {
                intentMode = std::nullopt;
            } else if (mode == "unit") {
                intentMode = EIntentMode::Unit;
            } else if (mode == "tile") {
                intentMode = EIntentMode::Tile;
            } else {
                return fail("data/ai profile " + id + ": intent_mode must be one of hybrid, unit, tile (got '" +
                            mode + "')");
            }
        }
        FNodeBuild root = builder.build(profile["tree"], "profile " + id);
        if (!root) {
            return fail("data/ai: " + root.error());
        }
        library.profiles_.push_back(FAiProfile{
            id, std::make_shared<const FBehaviorTree>(std::move(*root)), tuning, intentMode});
    }

    // ai.default is what every unit without an `ai` field runs; keep it first so fallback() is trivial.
    const auto defaultIt = std::ranges::find(library.profiles_, kDefaultProfileId, &FAiProfile::id);
    if (defaultIt == library.profiles_.end()) {
        return fail("data/ai: the profile '" + std::string(kDefaultProfileId) + "' is required");
    }
    std::iter_swap(library.profiles_.begin(), defaultIt);

    // Cross-table reference (R22): a unit may only name a profile that exists.
    for (const gameplay::FUnitContent& unit : content.units.rows) {
        if (!unit.aiProfile.empty() && library.find(unit.aiProfile) == nullptr) {
            return fail("data/ai: unit '" + unit.id + "' references unknown profile '" + unit.aiProfile + "'");
        }
    }
    return library;
}

std::optional<FAiLibrary> FAiLibrary::load(const FAssetManager& assets, const gameplay::FContentRegistry& content) {
    const std::shared_ptr<const FAssetDigest> digest = assets.digest("data/ai");
    if (digest == nullptr) {
        LOG_ERROR("ai: asset 'data/ai' is missing or unreadable.");
        return std::nullopt;
    }
    std::string error;
    std::optional<FAiLibrary> library = fromDocument(digest->json(), content, FAiNodeRegistry::builtin(), error);
    if (!library) {
        LOG_ERROR("ai: {}", error);
        return std::nullopt;
    }
    LOG_INFO("ai ready: {} profile(s).", library->profiles().size());
    return library;
}

const FAiProfile* FAiLibrary::find(std::string_view id) const {
    const auto it = std::ranges::find(profiles_, id, &FAiProfile::id);
    return it == profiles_.end() ? nullptr : &*it;
}

} // namespace game::ai
