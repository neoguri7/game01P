#pragma once

#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Logger.h"

namespace game {

struct FJsonLoadError {
    std::filesystem::path path;
    std::string message;
};

class FJsonLoader {
public:
    [[nodiscard]] static std::expected<nlohmann::json, FJsonLoadError> Load(
        const std::filesystem::path& path,
        int expectedSchemaVersion = 1
    ) {
        const auto fail = [&path](std::string message) {
            LOG_ERROR("JSON load failed ({}): {}", path.string(), message);
            return std::unexpected(FJsonLoadError{ path, std::move(message) });
        };

        std::ifstream input(path);
        if (!input) return fail("file could not be opened");

        nlohmann::json document;
        try {
            input >> document;
        } catch (const nlohmann::json::exception& exception) {
            return fail(std::string("parse error: ") + exception.what());
        }

        if (!document.is_object()) return fail("top-level JSON value must be an object");
        const auto schema = document.find("schema_version");
        if (schema == document.end() || !schema->is_number_integer()) {
            return fail("required integer field 'schema_version' is missing");
        }
        try {
            if (schema->get<int>() != expectedSchemaVersion) {
                return fail("unsupported schema_version: expected " + std::to_string(expectedSchemaVersion));
            }
        } catch (const nlohmann::json::exception& exception) {
            return fail(std::string("invalid schema_version: ") + exception.what());
        }

        return document;
    }
};

} // namespace game
