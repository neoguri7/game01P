// WP-55: macOS package builds only (GAME01P_PACKAGE=ON). Copies the bundle's Resources/assets into the cache
// directory and makes that directory the working directory, so the game sees the same layout as the Windows
// package: ./assets next to a writable ./game01p.log. A Finder launch starts in "/", which is not writable.
#include "core/EmbeddedAssets.h"
#include "embedded_assets.inc"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace game {

std::expected<std::filesystem::path, std::string> MountEmbeddedAssets()
{
    struct FRow {
        int id;
        const char* relativePath;
    };
#define GAME01P_ROW(id, path) {id, path},
    static const FRow rows[] = {GAME01P_EMBEDDED_ASSET_TABLE(GAME01P_ROW) {0, nullptr}};
#undef GAME01P_ROW

    std::vector<std::string_view> paths;
    for (const FRow& row : rows) {
        if (row.relativePath == nullptr) break;
        paths.emplace_back(row.relativePath);
    }

    const char* base = SDL_GetBasePath();
    if (base == nullptr) return std::unexpected(std::string("no base path: ") + SDL_GetError());

    const auto files = ReadAssetFiles(std::filesystem::path(base) / "assets", paths);
    if (!files) return std::unexpected(files.error());

    const char* overrideValue = std::getenv("GAME01P_PACKAGE_CACHE");
    std::error_code errorCode;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(errorCode);
    if (errorCode) return std::unexpected("no temp directory: " + errorCode.message());
    const std::filesystem::path root = ResolvePackageCacheRoot(
        overrideValue != nullptr ? std::string_view(overrideValue) : std::string_view(), temp, GAME01P_ASSET_PACK_HASH);

    const auto entries = files->entries();
    const auto synced = SyncEmbeddedAssets(root, entries);
    if (!synced) return std::unexpected(synced.error());

    std::filesystem::current_path(root, errorCode);
    if (errorCode) {
        const auto text = root.generic_u8string();
        return std::unexpected("cannot enter " + std::string(text.begin(), text.end()) + ": " + errorCode.message());
    }
    return root;
}

} // namespace game
