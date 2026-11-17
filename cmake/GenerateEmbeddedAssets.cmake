# WP-54: build-time generator for the single-file package. Run in script mode:
#   cmake -DASSET_DIR=<assets dir> -DOUT_DIR=<output dir> -P GenerateEmbeddedAssets.cmake
# Writes embedded_assets.rc (one RCDATA resource per asset file) and embedded_assets.inc (pack hash + id/path table).
# Both files are always rewritten: the .rc text does not change when an asset's bytes change, and its new timestamp
# is what makes the resource compiler run again.

if(NOT ASSET_DIR OR NOT OUT_DIR)
    message(FATAL_ERROR "GenerateEmbeddedAssets: pass -DASSET_DIR=<dir> and -DOUT_DIR=<dir>")
endif()

file(TO_CMAKE_PATH "${ASSET_DIR}" ASSET_DIR)
file(TO_CMAKE_PATH "${OUT_DIR}" OUT_DIR)

# Every file on disk is embedded, git-ignored ones included: the same set the developer build copies.
file(GLOB_RECURSE asset_files RELATIVE "${ASSET_DIR}" "${ASSET_DIR}/*")
list(SORT asset_files COMPARE STRING)

set(rc_text "")
set(table_text "")
set(hash_input "")
set(id 1000)
list(LENGTH asset_files asset_count)
set(index 0)
foreach(relative IN LISTS asset_files)
    set(absolute "${ASSET_DIR}/${relative}")
    file(SHA256 "${absolute}" file_hash)
    string(APPEND hash_input "${relative}=${file_hash}\n")
    string(APPEND rc_text "${id} RCDATA \"${absolute}\"\n")
    math(EXPR last_index "${asset_count} - 1")
    if(index LESS last_index)
        string(APPEND table_text "    X(${id}, \"${relative}\") \\\n")
    else()
        string(APPEND table_text "    X(${id}, \"${relative}\")\n")
    endif()
    math(EXPR id "${id} + 1")
    math(EXPR index "${index} + 1")
endforeach()

string(SHA256 pack_hash_full "${hash_input}")
string(SUBSTRING "${pack_hash_full}" 0 16 pack_hash)

file(MAKE_DIRECTORY "${OUT_DIR}")
file(WRITE "${OUT_DIR}/embedded_assets.rc" "${rc_text}")

set(inc_text "#define GAME01P_ASSET_PACK_HASH \"${pack_hash}\"\n")
if(asset_count EQUAL 0)
    string(APPEND inc_text "#define GAME01P_EMBEDDED_ASSET_TABLE(X)\n")
else()
    string(APPEND inc_text "#define GAME01P_EMBEDDED_ASSET_TABLE(X) \\\n${table_text}")
endif()
file(WRITE "${OUT_DIR}/embedded_assets.inc" "${inc_text}")
