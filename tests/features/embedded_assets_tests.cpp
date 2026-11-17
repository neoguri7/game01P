// WP-54 F3-F6: the pure part of the single-file package — unpacking embedded assets into a cache directory and
// choosing that directory.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include "core/EmbeddedAssets.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct FTempRoot {
    fs::path path;
    explicit FTempRoot(const char* name)
    {
        std::error_code error;
        path = fs::temp_directory_path(error) / (std::string("ember_embedded_") + name);
        fs::remove_all(path, error);
    }
    ~FTempRoot()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

struct FFixture {
    std::string small{"hello"};
    std::vector<std::byte> smallBytes;
    std::vector<std::byte> bigBytes;
    std::vector<std::byte> emptyBytes;
    std::vector<game::FEmbeddedAsset> assets;
    FFixture()
    {
        for (const char c : small) smallBytes.push_back(static_cast<std::byte>(c));
        for (int i = 0; i < 300; ++i) bigBytes.push_back(static_cast<std::byte>((i * 7 + 3) % 251));
        assets = {
            {"data/a.json", smallBytes},
            {"art/units/b.png", bigBytes},
            {"c.txt", emptyBytes},
        };
    }
};

std::vector<std::byte> ReadAll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::vector<std::byte> bytes;
    for (std::istreambuf_iterator<char> it(in), end; it != end; ++it) bytes.push_back(static_cast<std::byte>(*it));
    return bytes;
}

void SyncWritesNestedFiles()
{
    FTempRoot root("nested");
    FFixture fx;
    const auto result = game::SyncEmbeddedAssets(root.path, fx.assets);
    CHECK(result.has_value());
    CHECK(result->written == static_cast<int>(fx.assets.size()));
    CHECK(result->kept == 0);
    for (const auto& asset : fx.assets) {
        const auto file = root.path / "assets" / std::string(asset.relativePath);
        CHECK(fs::is_regular_file(file));
        CHECK(ReadAll(file) == std::vector<std::byte>(asset.bytes.begin(), asset.bytes.end()));
    }
}

void SyncKeepsMatchingAndRepairsDamaged()
{
    FTempRoot root("repair");
    FFixture fx;
    CHECK(game::SyncEmbeddedAssets(root.path, fx.assets).has_value());
    std::error_code error;
    const auto keptFile = root.path / "assets" / "c.txt";
    // push the kept file's timestamp into the past so an accidental rewrite is visible
    fs::last_write_time(keptFile, fs::file_time_type::clock::now() - std::chrono::hours(5), error);
    CHECK(!error);
    const auto before = fs::last_write_time(keptFile, error);
    fs::remove(root.path / "assets" / "data" / "a.json", error);
    CHECK(!error);
    fs::resize_file(root.path / "assets" / "art" / "units" / "b.png", 10, error);
    CHECK(!error);

    const auto result = game::SyncEmbeddedAssets(root.path, fx.assets);
    CHECK(result.has_value());
    CHECK(result->written == 2);
    CHECK(result->kept == static_cast<int>(fx.assets.size()) - 2);
    CHECK(fs::last_write_time(keptFile, error) == before);
    CHECK(ReadAll(root.path / "assets" / "data" / "a.json") == fx.smallBytes);
    CHECK(ReadAll(root.path / "assets" / "art" / "units" / "b.png") == fx.bigBytes);
    for (const auto& entry : fs::recursive_directory_iterator(root.path)) {
        CHECK(entry.path().filename().string().find(".tmp") == std::string::npos);
    }
}

void SyncReportsUnwritableRoot()
{
    FTempRoot root("unwritable");
    FFixture fx;
    std::error_code error;
    fs::create_directories(root.path.parent_path(), error);
    {
        std::ofstream out(root.path, std::ios::binary);
        out << "not a directory";
    }
    const auto result = game::SyncEmbeddedAssets(root.path, fx.assets);
    CHECK(!result.has_value());
    const auto text = root.path.generic_u8string();
    CHECK(result.error().find(std::string(text.begin(), text.end())) != std::string::npos);
    CHECK(fs::is_regular_file(root.path));
}

void CacheRootUsesOverrideThenTemp()
{
    const fs::path temp = fs::path("some") / "temp";
    CHECK(game::ResolvePackageCacheRoot("C:/over", temp, "abc123") == fs::path("C:/over") / "abc123");
    CHECK(game::ResolvePackageCacheRoot("", temp, "abc123") == temp / "EmberDungeon" / "abc123");
}

void WriteFile(const fs::path& file, const std::vector<std::byte>& bytes)
{
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void ReadAssetFilesReturnsBytesInOrder()
{
    FTempRoot root("readfiles");
    FTempRoot other("readfiles_other");
    FFixture fx;
    WriteFile(root.path / "data" / "a.json", fx.smallBytes);
    WriteFile(root.path / "art" / "units" / "b.png", fx.bigBytes);
    WriteFile(root.path / "c.txt", fx.emptyBytes);

    const std::vector<std::string_view> paths = {"c.txt", "art/units/b.png", "data/a.json"};
    const auto result = game::ReadAssetFiles(root.path, paths);
    CHECK(result.has_value());
    if (!result) return;
    CHECK(result->relativePaths == std::vector<std::string>({"c.txt", "art/units/b.png", "data/a.json"}));
    CHECK(result->contents.size() == 3);
    if (result->contents.size() != 3) return;
    CHECK(result->contents[0] == fx.emptyBytes);
    CHECK(result->contents[1] == fx.bigBytes);
    CHECK(result->contents[2] == fx.smallBytes);
    const auto entries = result->entries();
    CHECK(entries.size() == 3);
    if (entries.size() != 3) return;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        CHECK(entries[i].relativePath == paths[i]);
        CHECK(std::vector<std::byte>(entries[i].bytes.begin(), entries[i].bytes.end()) == result->contents[i]);
    }
    const auto synced = game::SyncEmbeddedAssets(other.path, entries);
    CHECK(synced.has_value());
    if (synced) CHECK(synced->written == 3);
}

void ReadAssetFilesReportsMissingFile()
{
    FTempRoot root("readfiles_missing");
    FFixture fx;
    WriteFile(root.path / "data" / "a.json", fx.smallBytes);
    const std::vector<std::string_view> paths = {"data/a.json", "data/missing.json"};
    const auto result = game::ReadAssetFiles(root.path, paths);
    CHECK(!result.has_value());
    if (!result) CHECK(result.error().find("missing.json") != std::string::npos);
}

} // namespace

int main()
{
    return game_test::RunTests("embedded_assets_tests", {
        {"SyncWritesNestedFiles", SyncWritesNestedFiles},
        {"SyncKeepsMatchingAndRepairsDamaged", SyncKeepsMatchingAndRepairsDamaged},
        {"SyncReportsUnwritableRoot", SyncReportsUnwritableRoot},
        {"CacheRootUsesOverrideThenTemp", CacheRootUsesOverrideThenTemp},
        {"ReadAssetFilesReturnsBytesInOrder", ReadAssetFilesReturnsBytesInOrder},
        {"ReadAssetFilesReportsMissingFile", ReadAssetFilesReportsMissingFile},
    });
}
