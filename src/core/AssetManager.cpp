#include "core/AssetManager.h"

#include "core/Logger.h"

#include <tracy/Tracy.hpp>

#include <fstream>
#include <sstream>
#include <utility>

namespace game {

FAssetManager::FAssetManager(std::vector<std::string> roots) : roots_(std::move(roots)) {}

void FAssetManager::Initialize(entt::registry& registry, std::vector<std::string> roots) {
    if (registry.ctx().contains<FAssetManager>()) {
        return;
    }
    const FAssetManager& assets = registry.ctx().emplace<FAssetManager>(std::move(roots));
    LOG_INFO("AssetManager ready ({} root(s), first='{}').", assets.roots().size(), assets.roots().empty() ? "" : assets.roots().front());
}

void FAssetManager::Shutdown(entt::registry& registry) {
    registry.ctx().erase<FAssetManager>();
}

std::shared_ptr<const FAssetDigest> FAssetManager::digest(std::string_view assetId) const {
    ZoneScopedN("FAssetManager::digest");

    const std::string key{assetId};
    const auto cached = cache_.find(key);
    if (cached != cache_.end()) {
        return cached->second;
    }

    const std::optional<std::string> path = resolvePath(assetId);
    if (!path) {
        LOG_ERROR("asset '{}' was not found in any root.", assetId);
        return nullptr;
    }
    const std::optional<std::string> text = readFile(*path);
    if (!text) {
        LOG_ERROR("asset '{}' could not be read ({}).", assetId, *path);
        return nullptr;
    }

    // Parsing is the last place an asset can fail, so it is also the last place that reports failure
    // (R22: decode once here; everything downstream holds typed data).
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(*text);
    } catch (const nlohmann::json::exception& error) {
        LOG_ERROR("asset '{}' is not valid JSON: {}", assetId, error.what());
        return nullptr;
    }

    std::shared_ptr<const FAssetDigest> loaded = std::make_shared<const FAssetDigest>(key, std::move(document));
    cache_.emplace(key, loaded);
    LOG_INFO("asset '{}' loaded from '{}'.", assetId, *path);
    return loaded;
}

std::optional<std::string> FAssetManager::resolvePath(std::string_view assetId) const {
    for (const std::string& root : roots_) {
        std::string candidate = root;
        candidate += '/';
        candidate.append(assetId);
        candidate += ".json";

        // Existence is tested by opening, so this boundary needs no filesystem library (R25).
        std::ifstream probe{candidate, std::ios::binary};
        if (probe.good()) {
            return candidate;
        }
    }
    return std::nullopt;
}

void FAssetManager::clearCache() {
    cache_.clear();
}

std::optional<std::string> FAssetManager::readFile(std::string_view path) const {
    std::ifstream file{std::string{path}, std::ios::binary};
    if (!file.good()) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace game
