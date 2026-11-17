#pragma once

#include "core/data/EContentFieldType.h"

#include <cstddef>
#include <string_view>

namespace game {

struct FContentSchema; // only pointed at from here; FContentSchema.h spells out the structure it owns

/// Upper bound on `ObjectArray` length when the field does not override `maxItems`: a data typo (an
/// accidentally duplicated block) fails the load instead of silently bloating the run.
inline constexpr std::size_t kDefaultMaxItems = 64;

/// One declared field of a content row: the key as spelled in the asset, its expected type, and
/// whether a row without it is a load error. Order of declared fields IS the projected row order.
///
/// `objectSchema` points at the statically declared sub-schema of an `Object`/`ObjectArray` field
/// (nullptr, and unused, for every flat type; a null pointer on an object field is a schema-authoring
/// bug the loader reports instead of dereferencing). Declare sub-schemas before the field arrays that
/// reference them so the constexpr pointer initializes. An *optional* self-referential `Object` would
/// make defaulting recurse forever, so a recursive schema must recurse through `ObjectArray` (a
/// missing array defaults to empty and never walks the schema).
struct FContentField {
    std::string_view key;
    EContentFieldType type = EContentFieldType::Text;
    bool required = true;
    const FContentSchema* objectSchema{nullptr}; // Object/ObjectArray only: statically declared sub-schema
    std::size_t maxItems{kDefaultMaxItems};      // ObjectArray only: length guard against data typos
};

} // namespace game
