# WP-36: runs game01P with one switch of the removed forest prototype mode and requires the rejection contract:
# exit code 2 and "forest mode was removed" on stderr.
# Usage: cmake -DEXE=<game01P path> -DARG=<argument> [-DMODE_ENV=<GAME01P_MODE value>] -P check_removed_flag.cmake
if(NOT DEFINED EXE OR NOT DEFINED ARG)
    message(FATAL_ERROR "check_removed_flag.cmake needs -DEXE=<game01P path> and -DARG=<argument>")
endif()
if(DEFINED MODE_ENV)
    set(ENV{GAME01P_MODE} "${MODE_ENV}")
endif()
execute_process(
    COMMAND "${EXE}" "${ARG}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
    TIMEOUT 30)
if(NOT result STREQUAL "2")
    message(FATAL_ERROR "expected exit code 2 for '${ARG}', got '${result}'. stderr: ${stderr_text}")
endif()
string(FIND "${stderr_text}" "forest mode was removed" found)
if(found EQUAL -1)
    message(FATAL_ERROR "stderr for '${ARG}' lacks 'forest mode was removed': ${stderr_text}")
endif()
message(STATUS "'${ARG}' rejected with exit code 2")
