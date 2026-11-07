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

/// The two names every loader message carries (WP-26): a nested read extends only the field path, but
/// `row '{}'` always names the top-level row a decoder can look up.
struct FLoadContext {
    std::string_view assetId;
    std::string_view rowId;
};

/// Builds one error-path step as `prefix + segment`. An empty prefix yields the segment alone, so the
/// top-level messages stay byte-identical to the pre-nesting format; nested paths grow from there
/// (`hooks[1].effect.kind`).
[[nodiscard]] std::string joinPath(std::string_view prefix, std::string_view segment) {
    std::string path;
    path.reserve(prefix.size() + segment.size());
    path.append(prefix);
    path.append(segment);
    return path;
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
    case EContentFieldType::Object:
        return "JSON object";
    case EContentFieldType::ObjectArray:
        return "JSON object array";
    }
    return "unknown";
}

/// An `Object`/`ObjectArray` field without a sub-schema is a schema-authoring bug: report it rather
/// than dereference. Checked on the read path and on the default path, so the failure never depends on
/// whether the asset happens to carry the field (WP-26).
[[nodiscard]] bool requireObjectSchema(const FContentField& field, std::string_view path, const FLoadContext& context) {
    if (field.objectSchema != nullptr) {
        return true;
    }
    LOG_ERROR("content asset '{}': field '{}' declares an object without a schema", context.assetId, path);
    return false;
}

/// Default value for a declared-but-optional field that the asset left out. Keeps the projected row
/// dense so decoders never have to test for a hole. Nested objects default recursively (WP-26): an
/// `Object` becomes one row of its child schema's defaults, an `ObjectArray` becomes empty.
[[nodiscard]] bool defaultOf(const FContentField& field, const std::string& path, const FLoadContext& context, FContentValue& out);

[[nodiscard]] bool defaultRow(const FContentSchema& schema, std::string_view pathPrefix, const FLoadContext& context, FContentRow& out) {
    out.values.reserve(schema.fields.size());
    for (const FContentField& field : schema.fields) {
        FContentValue value;
        if (!defaultOf(field, joinPath(pathPrefix, field.key), context, value)) {
            return false;
        }
        out.values.push_back(std::move(value));
    }
    return true;
}

[[nodiscard]] bool defaultOf(const FContentField& field, const std::string& path, const FLoadContext& context, FContentValue& out) {
    out = FContentValue{};
    out.type = field.type;
    switch (field.type) {
    case EContentFieldType::Text:
    case EContentFieldType::Id:
        out.value = std::string{};
        return true;
    case EContentFieldType::Number:
        out.value = 0.0;
        return true;
    case EContentFieldType::Boolean:
        out.value = false;
        return true;
    case EContentFieldType::IdArray:
        out.value = std::vector<std::string>{};
        return true;
    case EContentFieldType::NumberArray:
        out.value = std::vector<double>{};
        return true;
    case EContentFieldType::Object: {
        if (!requireObjectSchema(field, path, context)) {
            return false;
        }
        FContentRow nested;
        if (!defaultRow(*field.objectSchema, path + ".", context, nested)) {
            return false;
        }
        out.value = std::vector<FContentRow>{std::move(nested)};
        return true;
    }
    case EContentFieldType::ObjectArray:
        if (!requireObjectSchema(field, path, context)) {
            return false;
        }
        out.value = std::vector<FContentRow>{}; // a missing array has no elements to synthesize
        return true;
    }
    return false;
}

/// Validates one JSON object against `schema` and projects it into a dense row (WP-26). `pathPrefix`
/// is empty for a top-level row and ends in '.' / '[i].' for a nested object, so a failure can name
/// the exact place a value broke. The caller guarantees `element.is_object()`.
[[nodiscard]] bool readObject(const nlohmann::json& element, const FContentSchema& schema, std::string_view pathPrefix, const FLoadContext& context, FContentRow& out);

/// Converts one JSON value to `field.type`, or returns false after logging exactly one failure line
/// with the full path. This is the only place where an asset's text becomes a typed value (R22).
[[nodiscard]] bool readValue(const nlohmann::json& raw, const FContentField& field, const std::string& path, const FLoadContext& context, FContentValue& out) {
    out = FContentValue{};
    out.type = field.type;
    switch (field.type) {
    case EContentFieldType::Text:
    case EContentFieldType::Id:
        if (raw.is_string()) {
            out.value = raw.get<std::string>();
            return true;
        }
        break;
    case EContentFieldType::Number:
        if (raw.is_number()) {
            out.value = raw.get<double>();
            return true;
        }
        break;
    case EContentFieldType::Boolean:
        if (raw.is_boolean()) {
            out.value = raw.get<bool>();
            return true;
        }
        break;
    case EContentFieldType::IdArray: {
        if (!raw.is_array()) {
            break;
        }
        std::vector<std::string> items;
        items.reserve(raw.size());
        bool allStrings = true;
        for (const nlohmann::json& item : raw) {
            if (!item.is_string()) {
                allStrings = false;
                break;
            }
            items.push_back(item.get<std::string>());
        }
        if (allStrings) {
            out.value = std::move(items);
            return true;
        }
        break;
    }
    case EContentFieldType::NumberArray: {
        if (!raw.is_array()) {
            break;
        }
        std::vector<double> items;
        items.reserve(raw.size());
        bool allNumbers = true;
        for (const nlohmann::json& item : raw) {
            if (!item.is_number()) {
                allNumbers = false;
                break;
            }
            items.push_back(item.get<double>());
        }
        if (allNumbers) {
            out.value = std::move(items);
            return true;
        }
        break;
    }
    case EContentFieldType::Object: {
        if (!requireObjectSchema(field, path, context)) {
            return false;
        }
        if (raw.is_object()) {
            FContentRow nested;
            if (!readObject(raw, *field.objectSchema, path + ".", context, nested)) {
                return false; // already logged at the deepest failing point
            }
            out.value = std::vector<FContentRow>{std::move(nested)};
            return true;
        }
        break;
    }
    case EContentFieldType::ObjectArray: {
        if (!requireObjectSchema(field, path, context)) {
            return false;
        }
        if (!raw.is_array()) {
            break;
        }
        // Checked before any element is read: a data typo (a duplicated block) fails on its length,
        // not on whichever element would happen to break first.
        if (raw.size() > field.maxItems) {
            LOG_ERROR("content asset '{}': row '{}' field '{}' has {} items, more than the {} allowed", context.assetId, context.rowId, path, raw.size(), field.maxItems);
            return false;
        }
        std::vector<FContentRow> items;
        items.reserve(raw.size());
        for (std::size_t index = 0; index < raw.size(); ++index) {
            const std::string elementPath = path + "[" + std::to_string(index) + "]";
            if (!raw[index].is_object()) {
                LOG_ERROR("content asset '{}': row '{}' field '{}' must be a {}", context.assetId, context.rowId, elementPath, typeName(EContentFieldType::Object));
                return false;
            }
            FContentRow nested;
            if (!readObject(raw[index], *field.objectSchema, elementPath + ".", context, nested)) {
                return false; // already logged at the deepest failing point
            }
            items.push_back(std::move(nested));
        }
        out.value = std::move(items);
        return true;
    }
    }
    LOG_ERROR("content asset '{}': row '{}' field '{}' must be a {}", context.assetId, context.rowId, path, typeName(field.type));
    return false;
}

/// Validates one JSON object against `schema` and projects it into a dense row:
/// (a) reject undeclared keys first, then (b) read the declared fields in schema order — a missing
/// field becomes a required error or its default, a present value is type-checked recursively.
[[nodiscard]] bool readObject(const nlohmann::json& element, const FContentSchema& schema, std::string_view pathPrefix, const FLoadContext& context, FContentRow& out) {
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
            LOG_ERROR("content asset '{}': row '{}' has unknown field '{}'", context.assetId, context.rowId, joinPath(pathPrefix, entry.key()));
            return false;
        }
    }

    out.values.reserve(schema.fields.size());
    for (const FContentField& field : schema.fields) {
        const std::string path = joinPath(pathPrefix, field.key);
        const nlohmann::json::const_iterator valueIt = element.find(std::string{field.key}); // boundary: asset key
        FContentValue value;
        if (valueIt == element.end()) {
            if (field.required) {
                LOG_ERROR("content asset '{}': row '{}' is missing required field '{}'", context.assetId, context.rowId, path);
                return false;
            }
            if (!defaultOf(field, path, context, value)) {
                return false;
            }
        } else if (!readValue(*valueIt, field, path, context, value)) {
            return false; // the reason is already logged at the deepest point (WP-26)
        }
        out.values.push_back(std::move(value));
    }
    return true;
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

        // The row is validated and projected by the recursive reader: undeclared keys first, then the
        // declared fields in schema order. The empty path prefix keeps top-level messages byte-identical
        // to the pre-nesting format (WP-26 regression gate).
        const FLoadContext context{schema.assetId, rowId};
        FContentRow row;
        if (!readObject(element, schema, "", context, row)) {
            return std::nullopt;
        }
        rows.push_back(std::move(row));
    }

    LOG_INFO("content asset '{}': {} row(s) loaded", schema.assetId, rows.size());
    return rows;
}

} // namespace game
