#pragma once

namespace game {

/// Declared type of one content field. The loader validates an asset against these before conversion,
/// so a typo in a data file fails at load time instead of silently producing a default value (R22).
/// `Id`/`IdArray` carry the same storage as `Text`/`TextArray`; the distinction exists so a reference to
/// another table is declared as such instead of being re-derived by every decoder.
/// `Object`/`ObjectArray` carry nested rows validated recursively against `FContentField::objectSchema`
/// (WP-26): an `Object` projects one `FContentRow`, an `ObjectArray` projects one per element.
enum class EContentFieldType {
    Text,
    Id, // same storage as Text; the value names another row, and the *consumer* resolves it (the loader only checks storage, see FContentRegistry)
    Number,
    Boolean,
    IdArray, // same storage as TextArray, but every element names another row
    NumberArray,
    Object,      // one nested JSON object; requires FContentField::objectSchema
    ObjectArray, // a JSON array of nested objects; requires FContentField::objectSchema and honors maxItems
};

} // namespace game
