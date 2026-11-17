#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game {

struct FEmbeddedAsset {
    std::string_view relativePath;        // relative to the assets root, forward slashes: "data/units.json"
    std::span<const std::byte> bytes;
};
struct FEmbeddedSyncResult {
    int written{0};
    int kept{0};
};
/// The bytes of asset files read from disk (WP-55, macOS bundle). contents[i] belongs to relativePaths[i].
struct FAssetFileSet {
    std::vector<std::string> relativePaths;        // forward slashes, exactly as passed to ReadAssetFiles
    std::vector<std::vector<std::byte>> contents;
    /// One FEmbeddedAsset per file; the views point into this object and die with it.
    [[nodiscard]] std::vector<FEmbeddedAsset> entries() const;
};
/// Reads `<assetsDir>/<relativePath>` for every path, in order. Stops at the first file that cannot be opened
/// or fully read and returns "asset missing: <generic UTF-8 path of that file>".
[[nodiscard]] std::expected<FAssetFileSet, std::string> ReadAssetFiles(
    const std::filesystem::path& assetsDir, std::span<const std::string_view> relativePaths);

/// Makes `<root>/assets/<relativePath>` hold exactly `bytes` for every entry. A file whose size already equals
/// bytes.size() is kept. A missing or different-size file is written to `<file name>.tmp<process id>` in the same
/// directory and renamed over the target; if the rename fails and the target now has the right size (another
/// instance won the race), the temp file is removed and the entry counts as kept. Stops at the first failure and
/// returns a message that contains the failing path.
[[nodiscard]] std::expected<FEmbeddedSyncResult, std::string> SyncEmbeddedAssets(
    const std::filesystem::path& root, std::span<const FEmbeddedAsset> assets);

/// `<overrideDir>/<packHash>` when overrideDir is non-empty, else `<tempDir>/EmberDungeon/<packHash>`.
[[nodiscard]] std::filesystem::path ResolvePackageCacheRoot(
    std::string_view overrideDir, const std::filesystem::path& tempDir, std::string_view packHash);

/// Package builds only; declared here so main.cpp sees it. Windows (package/EmbeddedAssetMount.cpp): unpacks the
/// assets embedded as resources. macOS (package/BundleAssetMount.cpp): copies the bundle's Resources/assets.
/// Both fill the cache root and make it the working directory. Returns the cache root. Must run before anything
/// opens a file or creates the logger.
[[nodiscard]] std::expected<std::filesystem::path, std::string> MountEmbeddedAssets();

} // namespace game
