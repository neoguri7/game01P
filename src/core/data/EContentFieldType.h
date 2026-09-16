#pragma once

namespace game {

/// Declared type of one content field. The loader validates an asset against these before conversion,
/// so a typo in a data file fails at load time instead of silently producing a default value (R22).
/// `Id`/`IdArray` carry the same storage as `Text`/`TextArray`; the distinction exists so a reference to
/// another table is declared as such instead of being re-derived by every decoder.
enum class EContentFieldType {
    Text,
    Id, // same storage as Text, but the value names another row (validated as a cross-table reference)
    Number,
    Boolean,
    IdArray, // same storage as TextArray, but every element names another row
    NumberArray,
};

} // namespace game
