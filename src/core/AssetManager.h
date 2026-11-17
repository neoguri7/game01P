#pragma once

#include "core/FAssetDigest.h"

#include <entt/entt.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game {

/// The file boundary (R25/R23): maps logical asset ids to root-based paths and caches immutable JSON
/// documents. PNG decoding stays in FResourceManager; gameplay and systems do not manipulate paths.
///
/// Lives in the EnTT context as a service. Parsing happens once per id; every later request returns the
/// same immutable document.
struct FAssetManager {
    /// `roots` are searched in order, so a mod/patch directory can be added ahead of the shipped one.
    explicit FAssetManager(std::vector<std::string> roots = {"assets"});

    static void Initialize(entt::registry& registry, std::vector<std::string> roots = {"assets"});

    static void Shutdown(entt::registry& registry);

    /// Loads (or returns the already parsed) document for `assetId`. Returns nullptr when the asset is
    /// unknown or unreadable — the reason is in the log, not in the return value (R19).
    [[nodiscard]] std::shared_ptr<const FAssetDigest> digest(std::string_view assetId) const;

    /// The on-disk path an id and extension resolve to; extension defaults to .json.
    [[nodiscard]] std::optional<std::string> resolvePath(std::string_view assetId, std::string_view extension = ".json") const;

    /// Drops every cached document. Exists so a reload/debug path does not need the manager rebuilt.
    void clearCache();

    [[nodiscard]] const std::vector<std::string>& roots() const { return roots_; }

private:
    // Boundary: only this translation unit turns an id into a path and reads bytes.
    [[nodiscard]] std::optional<std::string> readFile(std::string_view path) const;

    std::vector<std::string> roots_;
    // A cache is the sanctioned reason for `mutable` (the const query still answers "do I know this id?").
    mutable std::unordered_map<std::string, std::shared_ptr<const FAssetDigest>> cache_;
};

} // namespace game
