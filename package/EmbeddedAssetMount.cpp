// WP-54: package builds only (GAME01P_PACKAGE=ON). Finds the assets embedded as RCDATA resources, unpacks them into
// the cache directory and makes that directory the working directory. No logger here: this runs before it exists.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/EmbeddedAssets.h"
#include "embedded_assets.inc"

#include <cstdlib>
#include <cstddef>
#include <filesystem>
#include <string>
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

    std::vector<FEmbeddedAsset> assets;
    for (const FRow& row : rows) {
        if (row.relativePath == nullptr) break;
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(row.id), MAKEINTRESOURCEW(10));
        HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
        const void* data = loaded ? LockResource(loaded) : nullptr;
        if (resource == nullptr || loaded == nullptr || data == nullptr) {
            return std::unexpected(std::string("embedded asset missing: ") + row.relativePath);
        }
        assets.push_back({row.relativePath,
                          std::span<const std::byte>(static_cast<const std::byte*>(data), SizeofResource(nullptr, resource))});
    }

    const char* overrideValue = std::getenv("GAME01P_PACKAGE_CACHE");
    std::error_code errorCode;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(errorCode);
    if (errorCode) return std::unexpected("no temp directory: " + errorCode.message());
    const std::filesystem::path root = ResolvePackageCacheRoot(
        overrideValue != nullptr ? std::string_view(overrideValue) : std::string_view(), temp, GAME01P_ASSET_PACK_HASH);

    const auto synced = SyncEmbeddedAssets(root, assets);
    if (!synced) return std::unexpected(synced.error());

    std::filesystem::current_path(root, errorCode);
    if (errorCode) {
        const auto text = root.generic_u8string();
        return std::unexpected("cannot enter " + std::string(text.begin(), text.end()) + ": " + errorCode.message());
    }
    return root;
}

} // namespace game
