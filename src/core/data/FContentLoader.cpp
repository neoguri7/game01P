#include "core/data/FContentLoader.h"

#include "core/Logger.h"

#include <nlohmann/json.hpp>
#include <tracy/Tracy.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace game {
namespace {

/// Default value for a declared-but-optional field that the asset left out. Keeps the projected row
/// dense so decoders never have to test for a hole.
[[nodiscard]] FContentValue defaultOf(EContentFieldType type) {
    FContentValue value;
    value.type = type;
    switch (type) {
    case EContentFieldType::Text:
    case EContentFieldType::Id:
        value.value = std::string{};
        break;
    case EContentFieldType::Number:
        value.value = 0.0;
        break;
    case EContentFieldType::Boolean:
        value.value = false;
        break;
    case EContentFieldType::IdArray:
        value.value = std::vector<std::string>{};
        break;
    case EContentFieldType::NumberArray:
        value.value = std::vector<double>{};
        break;
    }
    return value;
}

[[nodiscard]] const char* typeName(EContentFieldType type) {
    switch (type) {
    case EContentFieldType::Text:
        return "text";
    case EContentFieldType::Id:
        return "id";
    case EContentFieldType::Number:
        return "number";
    case EContentFieldType::Boolean:
        return "boolean";
    case EContentFieldType::IdArray:
        return "id array";
    case EContentFieldType::NumberArray:
        return "number array";
    }
    return "unknown";
}

/// Converts one JSON value to `type`, or reports why it cannot and returns false. This is the only
/// place where an asset's text becomes a typed value (R22).
[[nodiscard]] bool readValue(const nlohmann::json& raw, EContentFieldType type, FContentValue& out) {
    out = defaultOf(type);
    switch (type) {
    case EContentFieldType::Text:
    case EContentFieldType::Id: {
        if (!raw.is_string()) {
            return false;
        }
        out.value = raw.get<std::string>();
        return true;
    }
    case EContentFieldType::Number: {
        if (!raw.is_number()) {
            return false;
        }
        out.value = raw.get<double>();
        return true;
    }
    case EContentFieldType::Boolean: {
        if (!raw.is_boolean()) {
            return false;
        }
        out.value = raw.get<bool>();
        return true;
    }
    case EContentFieldType::IdArray: {
        if (!raw.is_array()) {
            return false;
        }
        std::vector<std::string> items;
        items.reserve(raw.size());
        for (const nlohmann::json& item : raw) {
            if (!item.is_string()) {
                return false;
            }
            items.push_back(item.get<std::string>());
        }
        out.value = std::move(items);
        return true;
    }
    case EContentFieldType::NumberArray: {
        if (!raw.is_array()) {
            return false;
        }
        std::vector<double> items;
        items.reserve(raw.size());
        for (const nlohmann::json& item : raw) {
            if (!item.is_number()) {
                return false;
            }
            items.push_back(item.get<double>());
        }
        out.value = std::move(items);
        return true;
    }
    }
    return false;
}

} // namespace

std::optional<std::vector<FContentRow>> FContentLoader::load(const FAssetManager& assets, const FContentSchema& schema) {
    ZoneScopedN("FContentLoader::load");
    const std::shared_ptr<const FAssetDigest> digest = assets.digest(schema.assetId);
    if (!digest) {
        LOG_ERROR("content asset '{}' is missing or failed to parse", schema.assetId);
        return std::nullopt;
    }
    const nlohmann::json& document = digest->json();

    // A document that is not an object is a broken asset, not a reason to abort the process: every
    // content failure reports itself and stops the boot (R19). Reading a key out of a non-object would
    // throw instead of returning, which is exactly the untested failure mode this guard removes.
    if (!document.is_object()) {
        LOG_ERROR("content asset '{}': document must be an object", schema.assetId);
        return std::nullopt;
    }

    // Version is part of the asset boundary: a mismatch means the schema and the file disagree, and
    // guessing which side is stale would hide the mistake.
    const nlohmann::json::const_iterator versionIt = document.find("schema_version"); // boundary: asset key
    if (versionIt == document.end() || !versionIt->is_number_integer()) {
        LOG_ERROR("content asset '{}': missing integer schema_version", schema.assetId);
        return std::nullopt;
    }
    const int assetVersion = versionIt->get<int>();
    if (assetVersion != schema.schemaVersion) {
        LOG_ERROR("content asset '{}': schema_version is {}, expected {}", schema.assetId, assetVersion, schema.schemaVersion);
        return std::nullopt;
    }

    const nlohmann::json::const_iterator rowsIt = document.find(std::string{schema.rowKey}); // boundary: asset key
    if (rowsIt == document.end() || !rowsIt->is_array()) {
        LOG_ERROR("content asset '{}': '{}' must be an array", schema.assetId, schema.rowKey);
        return std::nullopt;
    }

    std::size_t idFieldIndex = schema.fields.size();
    for (std::size_t index = 0; index < schema.fields.size(); ++index) {
        // `Id` is accepted for the identity field: it is the declaration that says "this value names a row",
        // so refusing it here would make the type unusable for the one field it describes (R20).
        if (schema.fields[index].key == schema.idField
            && (schema.fields[index].type == EContentFieldType::Text || schema.fields[index].type == EContentFieldType::Id)) {
            idFieldIndex = index;
        }
    }
    if (idFieldIndex == schema.fields.size()) {
        LOG_ERROR("content asset '{}': id field '{}' is not a declared text or id field", schema.assetId, schema.idField);
        return std::nullopt;
    }

    std::vector<FContentRow> rows;
    rows.reserve(rowsIt->size());
    // Insert-only: membership is the whole question here, and the set is never iterated, so it cannot
    // make any result depend on hash order (R16).
    std::unordered_set<std::string> seenIds;
    for (const nlohmann::json& element : *rowsIt) {
        if (!element.is_object()) {
            LOG_ERROR("content asset '{}': '{}' entries must be objects", schema.assetId, schema.rowKey);
            return std::nullopt;
        }

        // Row identity comes first so every later message in this row can name it.
        const nlohmann::json::const_iterator idIt = element.find(std::string{schema.idField}); // boundary: asset key
        if (idIt == element.end() || !idIt->is_string() || idIt->get_ref<const std::string&>().empty()) {
            LOG_ERROR("content asset '{}': a '{}' entry has no non-empty '{}'", schema.assetId, schema.rowKey, schema.idField);
            return std::nullopt;
        }
        const std::string rowId = idIt->get<std::string>();

        // A duplicate id would be a silent shadow: FContentTable::find returns the first match, so the
        // later row would never be reachable and nothing would say so (R22).
        if (!seenIds.insert(rowId).second) {
            LOG_ERROR("content asset '{}': duplicate id '{}'", schema.assetId, rowId);
            return std::nullopt;
        }

        // Reject keys nobody declared: a typo must be an error, not a silently ignored field (R19).
        // `items()` yields key/value proxies, so the loop variable is deduced (R22: the only place a raw
        // map key is compared against a declared field name).
        for (const auto& entry : element.items()) { // boundary: asset key
            bool declared = false;
            for (const FContentField& field : schema.fields) {
                if (field.key == entry.key()) {
                    declared = true;
                    break;
                }
            }
            if (!declared) {
                LOG_ERROR("content asset '{}': row '{}' has unknown field '{}'", schema.assetId, rowId, entry.key());
                return std::nullopt;
            }
        }

        FContentRow row;
        row.values.reserve(schema.fields.size());
        for (const FContentField& field : schema.fields) {
            const nlohmann::json::const_iterator valueIt = element.find(std::string{field.key}); // boundary: asset key
            if (valueIt == element.end()) {
                if (field.required) {
                    LOG_ERROR("content asset '{}': row '{}' is missing required field '{}'", schema.assetId, rowId, field.key);
                    return std::nullopt;
                }
                row.values.push_back(defaultOf(field.type));
                continue;
            }
            FContentValue value;
            if (!readValue(*valueIt, field.type, value)) {
                LOG_ERROR("content asset '{}': row '{}' field '{}' must be a {}", schema.assetId, rowId, field.key, typeName(field.type));
                return std::nullopt;
            }
            row.values.push_back(std::move(value));
        }
        rows.push_back(std::move(row));
    }

    LOG_INFO("content asset '{}': {} row(s) loaded", schema.assetId, rows.size());
    return rows;
}

} // namespace game
