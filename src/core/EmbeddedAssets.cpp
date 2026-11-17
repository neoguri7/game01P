#include "core/EmbeddedAssets.h"

#include <cstdint>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

// No logger here: this code runs before FLogger::Initialize (see MountEmbeddedAssets).

namespace game {

namespace {

namespace fs = std::filesystem;

std::string Utf8(const fs::path& path)
{
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

int ProcessId()
{
#if defined(_WIN32)
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

std::string Failure(const char* what, const fs::path& path, const std::error_code& error)
{
    std::string message = std::string(what) + " " + Utf8(path);
    if (error) message += ": " + error.message();
    return message;
}

/// True when `file` is a regular file of exactly `size` bytes.
bool HasSize(const fs::path& file, std::uintmax_t size)
{
    std::error_code error;
    if (!fs::is_regular_file(file, error) || error) return false;
    const auto actual = fs::file_size(file, error);
    return !error && actual == size;
}

} // namespace

std::vector<FEmbeddedAsset> FAssetFileSet::entries() const
{
    std::vector<FEmbeddedAsset> result;
    result.reserve(relativePaths.size());
    for (std::size_t i = 0; i < relativePaths.size(); ++i) {
        result.push_back({relativePaths[i], std::span<const std::byte>(contents[i])});
    }
    return result;
}

std::expected<FAssetFileSet, std::string> ReadAssetFiles(
    const fs::path& assetsDir, std::span<const std::string_view> relativePaths)
{
    FAssetFileSet set;
    set.relativePaths.reserve(relativePaths.size());
    set.contents.reserve(relativePaths.size());
    for (const std::string_view relative : relativePaths) {
        const fs::path file = assetsDir / fs::path(std::string(relative));
        const std::string missing = "asset missing: " + Utf8(file);
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        if (!in) return std::unexpected(missing);
        const std::streamoff size = static_cast<std::streamoff>(in.tellg());
        if (size < 0) return std::unexpected(missing);
        in.seekg(0);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        if (size > 0) {
            in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
            if (!in) return std::unexpected(missing);
        }
        set.relativePaths.emplace_back(relative);
        set.contents.push_back(std::move(bytes));
    }
    return set;
}

std::expected<FEmbeddedSyncResult, std::string> SyncEmbeddedAssets(
    const fs::path& root, std::span<const FEmbeddedAsset> assets)
{
    FEmbeddedSyncResult result;
    for (const auto& asset : assets) {
        const fs::path target = root / "assets" / fs::path(std::string(asset.relativePath));
        if (HasSize(target, asset.bytes.size())) {
            ++result.kept;
            continue;
        }

        std::error_code error;
        fs::create_directories(target.parent_path(), error);
        if (error) return std::unexpected(Failure("cannot create directory", target.parent_path(), error));

        fs::path temp = target;
        temp.replace_filename(target.filename().string() + ".tmp" + std::to_string(ProcessId()));
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return std::unexpected(Failure("cannot write", temp, {}));
            if (!asset.bytes.empty()) {
                out.write(reinterpret_cast<const char*>(asset.bytes.data()),
                          static_cast<std::streamsize>(asset.bytes.size()));
            }
            out.close();
            if (!out) {
                fs::remove(temp, error);
                return std::unexpected(Failure("cannot write", temp, {}));
            }
        }

        fs::rename(temp, target, error);
        if (error) {
            const std::error_code renameError = error;
            // Another instance may have won the race and left a complete file in place.
            const bool raceWon = HasSize(target, asset.bytes.size());
            fs::remove(temp, error);
            if (raceWon) {
                ++result.kept;
                continue;
            }
            return std::unexpected(Failure("cannot replace", target, renameError));
        }
        ++result.written;
    }
    return result;
}

fs::path ResolvePackageCacheRoot(std::string_view overrideDir, const fs::path& tempDir, std::string_view packHash)
{
    const fs::path hash{std::string(packHash)};
    if (!overrideDir.empty()) return fs::path(std::string(overrideDir)) / hash;
    return tempDir / "EmberDungeon" / hash;
}

} // namespace game
