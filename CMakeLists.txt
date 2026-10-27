cmake_minimum_required(VERSION 3.24)
project(PlaydenLauncher VERSION 0.8.0 LANGUAGES C CXX)

set(PLAYDEN_PRERELEASE "" CACHE STRING "Pre-release suffix: empty, beta.N (1-999999) or nightly.YYYYMMDD.N (1-999)")
set(PLAYDEN_FULL_VERSION "${PROJECT_VERSION}")
if(NOT PLAYDEN_PRERELEASE STREQUAL "")
    if(NOT PLAYDEN_PRERELEASE MATCHES "^beta\\.[1-9][0-9]?[0-9]?[0-9]?[0-9]?[0-9]?$" AND
       NOT PLAYDEN_PRERELEASE MATCHES "^nightly\\.20[0-9][0-9][01][0-9][0-3][0-9]\\.[1-9][0-9]?[0-9]?$")
        message(FATAL_ERROR "PLAYDEN_PRERELEASE must be empty, beta.N (1-999999) or nightly.YYYYMMDD.N (1-999)")
    endif()
    string(APPEND PLAYDEN_FULL_VERSION "-${PLAYDEN_PRERELEASE}")
endif()

# The commit the build is made from, for Settings › About. Configure runs again whenever HEAD moves.
set(PLAYDEN_BUILD_COMMIT "")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=10 HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE PLAYDEN_BUILD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE playden_git_result)
    if(NOT playden_git_result EQUAL 0 OR NOT PLAYDEN_BUILD_COMMIT MATCHES "^[0-9a-f]+$")
        set(PLAYDEN_BUILD_COMMIT "")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --path-format=absolute --git-path logs/HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE playden_head_log OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(playden_head_log AND EXISTS "${playden_head_log}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${playden_head_log}")
    endif()
endif()

option(PLAYDEN_UI_SIMULATION "Package selected PS5 UI with developer simulation fixtures" OFF)
option(PLAYDEN_PLATFORM_PS5 "Build a PS5 native title" OFF)
set(PLAYDEN_PS5_TITLE_ID "" CACHE STRING "Explicit PS5 title id (required for PS5 packaging)")

file(READ "${CMAKE_SOURCE_DIR}/dependencies.lock.json" playden_dependency_lock)
include(CTest)
include(cmake/PlaydenCore.cmake)
include(cmake/SteamBackend.cmake)
playden_register_steam_backend_tests()
include(cmake/ProsperoRuntime.cmake)
include(cmake/SelfUpdate.cmake)
include(cmake/HomebrewUI.cmake)
