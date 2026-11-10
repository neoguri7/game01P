# CMakeList.txt : CMake project for game01P, include source and define
#
cmake_minimum_required (VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDevInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDevInfo>:ProgramDatabase>>")
  endif()
endif()

project ("game01P")

# The Tracy profiler client opens a listener on all interfaces at startup, so every new exe path gets a Windows
# Firewall prompt. TRACY_ONLY_LOCALHOST is compile-time-only in Tracy 0.14.1, so opting out has to be a build option.
# OFF keeps the Tracy headers (they still compile, macros are no-ops without TRACY_ENABLE) but links no client.
option(GAME01P_ENABLE_TRACY "Link the Tracy profiler client (listens on the network at startup)" ON)

# Recursively find all source and header files in the src directory.
# CONFIGURE_DEPENDS ensures CMake re-runs if files are added or removed.
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
    "src/*.hpp"
)

# Add source to this project's executable.
list(FILTER SRC_FILES EXCLUDE REGEX "/main\\.cpp$")
add_library(game01P_runtime STATIC ${SRC_FILES})
add_executable(game01P "src/main.cpp")
target_link_libraries(game01P PRIVATE game01P_runtime)

# Add the src directory as an include path #include "core/Game.h"
target_include_directories(game01P_runtime PUBLIC "src")

target_compile_options(game01P_runtime PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
	$<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

find_package(SDL3 CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(game01P_runtime PRIVATE ${MINIAUDIO_INCLUDE_DIRS})

target_link_libraries(game01P_runtime
	PUBLIC
		SDL3::SDL3
		EnTT::EnTT
		glm::glm
		imgui::imgui
		$<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
		SDL3_ttf::SDL3_ttf
		spdlog::spdlog
		nlohmann_json::nlohmann_json
)

if(GAME01P_ENABLE_TRACY)
	# Default: the profiler client, exactly as before the option existed.
	target_link_libraries(game01P_runtime PUBLIC Tracy::TracyClient)
else()
	# Profiler off: still expose the Tracy include directory so src/ headers that #include <tracy/Tracy.hpp> compile
	# (without TRACY_ENABLE the macros are no-ops). PUBLIC because the test targets include those headers too.
	target_include_directories(game01P_runtime PUBLIC
		$<TARGET_PROPERTY:Tracy::TracyClient,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

set_target_properties(game01P game01P_runtime PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_compile_options(game01P PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

include(CTest)
if(BUILD_TESTING)
    add_executable(dungeon_tests "tests/DungeonTests.cpp")
    target_link_libraries(dungeon_tests PRIVATE game01P_runtime)
    set_target_properties(dungeon_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
    add_test(NAME dungeon_rules COMMAND dungeon_tests)
    set_tests_properties(dungeon_rules PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(ai_tests "tests/AiTests.cpp")
    target_link_libraries(ai_tests PRIVATE game01P_runtime)
    set_target_properties(ai_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
    add_test(NAME ai_behavior COMMAND ai_tests)
    set_tests_properties(ai_behavior PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME dungeon_title_smoke COMMAND game01P --ember --smoke-test)
    add_test(NAME dungeon_battle_smoke COMMAND game01P --ember --smoke-test)
    add_test(NAME forest_title_smoke COMMAND game01P --forest --smoke-test)
    add_test(NAME forest_battle_smoke COMMAND game01P --forest --smoke-test)
    add_test(NAME forest_prototype_checks COMMAND game01P --forest --verify-prototype)
    set_tests_properties(dungeon_title_smoke dungeon_battle_smoke forest_title_smoke forest_battle_smoke forest_prototype_checks PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:game01P>"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
        TIMEOUT 30
        RUN_SERIAL TRUE
        FIXTURES_REQUIRED AssetStage)
    # The prototype verifier runs ~26 s of scripted checks on a software renderer: give it headroom over the 30 s.
    set_tests_properties(forest_prototype_checks PROPERTIES TIMEOUT 90)
    set_property(TEST dungeon_battle_smoke APPEND PROPERTY ENVIRONMENT "GAME01P_START=battle")
    set_property(TEST forest_battle_smoke APPEND PROPERTY ENVIRONMENT "GAME01P_START=battle")
    # WP-00b: every test that boots the game exe writes its saves under the build tree, never the user's
    # %APPDATA%\Game01P\EmberDungeon. One directory per test keeps the runs independent.
    set(SMOKE_TESTS dungeon_title_smoke dungeon_battle_smoke forest_title_smoke forest_battle_smoke forest_prototype_checks)
    foreach(smoke_test IN LISTS SMOKE_TESTS)
        set_property(TEST ${smoke_test} APPEND PROPERTY ENVIRONMENT
            "GAME01P_SAVE_DIR=${CMAKE_BINARY_DIR}/test-saves/${smoke_test}")
    endforeach()
    # B7: always run asset staging. A JSON-only change does not relink game01P, so the POST_BUILD copy below does not
    # run; without this fixture the smoke tests would read stale assets.
    add_test(NAME stage_assets COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:game01P>/assets")
    set_tests_properties(stage_assets PROPERTIES FIXTURES_SETUP AssetStage)
    if(WIN32)
        add_custom_command(TARGET dungeon_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:dungeon_tests> $<TARGET_FILE_DIR:dungeon_tests>
            COMMAND_EXPAND_LISTS)
        add_custom_command(TARGET ai_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:ai_tests> $<TARGET_FILE_DIR:ai_tests>
            COMMAND_EXPAND_LISTS)
    endif()
    # Feature tests: every tests/features/*_tests.cpp becomes its own executable + ctest entry.
    # A later WP adds a test file and needs no CMake edit.
    file(GLOB FEATURE_TEST_SOURCES CONFIGURE_DEPENDS "tests/features/*_tests.cpp")
    foreach(source IN LISTS FEATURE_TEST_SOURCES)
        get_filename_component(name "${source}" NAME_WE)
        add_executable(${name} "${source}")
        target_link_libraries(${name} PRIVATE game01P_runtime)
        set_target_properties(${name} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
        add_test(NAME ${name} COMMAND ${name})
        # One save directory per feature test (WP-00b follow-up): without it every test falls back to the caller's
        # GAME01P_SAVE_DIR, so concurrent or back-to-back runs share checkpoint/run-save files and flake.
        set_tests_properties(${name} PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            ENVIRONMENT "GAME01P_SAVE_DIR=${CMAKE_BINARY_DIR}/test-saves/${name}"
            TIMEOUT 120)
        if(WIN32)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${name}> $<TARGET_FILE_DIR:${name}>
                COMMAND_EXPAND_LISTS)
        endif()
    endforeach()
endif()

add_custom_command(TARGET game01P POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/assets"
		"$<TARGET_FILE_DIR:game01P>/assets"
)

if (WIN32)
	add_custom_command(TARGET game01P POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_RUNTIME_DLLS:game01P>
			$<TARGET_FILE_DIR:game01P>
		COMMAND_EXPAND_LISTS
	)
endif()
