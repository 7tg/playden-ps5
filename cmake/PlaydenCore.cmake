# Renderer-independent Playden storage and service targets.

find_package(Threads REQUIRED)

foreach(utf8proc_file utf8proc.c utf8proc.h utf8proc_data.c LICENSE.md)
    string(JSON expected_utf8proc_hash GET "${playden_dependency_lock}"
        utf8proc files "${utf8proc_file}")
    set(utf8proc_path "${CMAKE_SOURCE_DIR}/third_party/utf8proc/${utf8proc_file}")
    file(SHA256 "${utf8proc_path}" actual_utf8proc_hash)
    if(NOT actual_utf8proc_hash STREQUAL expected_utf8proc_hash)
        message(FATAL_ERROR "Pinned utf8proc source hash mismatch: ${utf8proc_file}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${utf8proc_path}")
endforeach()

add_library(playden_library STATIC
    "${CMAKE_SOURCE_DIR}/third_party/utf8proc/utf8proc.c"
    "${CMAKE_SOURCE_DIR}/src/library/query.cpp"
    "${CMAKE_SOURCE_DIR}/src/library/cache.cpp"
    "${CMAKE_SOURCE_DIR}/src/library/organization.cpp")
target_include_directories(playden_library PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party/utf8proc")
target_compile_definitions(playden_library PRIVATE UTF8PROC_STATIC)
target_compile_features(playden_library PUBLIC cxx_std_17)

add_library(playden_storage STATIC
    "${CMAKE_SOURCE_DIR}/src/platform/storage.cpp")
target_include_directories(playden_storage PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_storage PUBLIC cxx_std_17)
target_link_libraries(playden_library PUBLIC playden_storage)

add_library(playden_steam_core STATIC
    "${CMAKE_SOURCE_DIR}/src/steam/session_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/service.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/app_details_service.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/install_service.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/install_volumes.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/cloud_service.cpp")
target_include_directories(playden_steam_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_steam_core PUBLIC cxx_std_17)
if(PLAYDEN_PLATFORM_PS5)
    target_compile_definitions(playden_steam_core PRIVATE PLAYDEN_PLATFORM_PS5=1)
endif()
target_link_libraries(playden_steam_core PUBLIC playden_storage Threads::Threads)

add_library(playden_library_service STATIC
    "${CMAKE_SOURCE_DIR}/src/library/service.cpp")
target_include_directories(playden_library_service PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_library_service PUBLIC cxx_std_17)
target_link_libraries(playden_library_service PUBLIC playden_steam_core)

add_library(playden_diagnostic_log STATIC "${CMAKE_SOURCE_DIR}/src/app/diagnostic_log.cpp")
target_include_directories(playden_diagnostic_log PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_diagnostic_log PUBLIC cxx_std_17)

add_library(playden_preferences STATIC "${CMAKE_SOURCE_DIR}/src/app/preferences.cpp")
target_include_directories(playden_preferences PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_preferences PUBLIC cxx_std_17)
target_link_libraries(playden_preferences PUBLIC playden_storage)

add_library(playden_persistence_service STATIC "${CMAKE_SOURCE_DIR}/src/app/persistence_service.cpp")
target_include_directories(playden_persistence_service PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_persistence_service PUBLIC cxx_std_17)
target_link_libraries(playden_persistence_service PUBLIC playden_preferences playden_library Threads::Threads)

add_library(playden_game_paths STATIC "${CMAKE_SOURCE_DIR}/src/steam/backend/game_path.cpp")
target_include_directories(playden_game_paths PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_game_paths PUBLIC cxx_std_17)

add_library(playden_runtime_installs STATIC "${CMAKE_SOURCE_DIR}/src/runtime/game_install.cpp")
target_include_directories(playden_runtime_installs PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_runtime_installs PUBLIC cxx_std_17)
target_link_libraries(playden_runtime_installs PUBLIC playden_storage playden_steam_protocol playden_game_paths)

add_library(playden_native_access STATIC "${CMAKE_SOURCE_DIR}/src/platform/ps5/native_access.cpp")
target_include_directories(playden_native_access PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_native_access PUBLIC cxx_std_17)

if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
    add_executable(native_access_test "${CMAKE_SOURCE_DIR}/tests/native_access_test.cpp")
    target_link_libraries(native_access_test PRIVATE playden_native_access)
    add_test(NAME native_access COMMAND native_access_test)
    add_executable(runtime_game_install_test "${CMAKE_SOURCE_DIR}/tests/runtime_game_install_test.cpp")
    target_link_libraries(runtime_game_install_test PRIVATE playden_runtime_installs)
    add_test(NAME runtime_game_install COMMAND runtime_game_install_test)
    add_executable(diagnostic_log_test "${CMAKE_SOURCE_DIR}/tests/diagnostic_log_test.cpp")
    target_link_libraries(diagnostic_log_test PRIVATE playden_diagnostic_log)
    add_test(NAME diagnostic_log COMMAND diagnostic_log_test)
    add_executable(preferences_test "${CMAKE_SOURCE_DIR}/tests/preferences_test.cpp")
    target_link_libraries(preferences_test PRIVATE playden_preferences)
    add_test(NAME preferences COMMAND preferences_test)
    add_executable(persistence_service_test "${CMAKE_SOURCE_DIR}/tests/persistence_service_test.cpp")
    target_link_libraries(persistence_service_test PRIVATE playden_persistence_service)
    add_test(NAME persistence_service COMMAND persistence_service_test)
    add_executable(library_sync_test "${CMAKE_SOURCE_DIR}/tests/library_sync_test.cpp")
    target_link_libraries(library_sync_test PRIVATE playden_library_service)
    add_test(NAME library_sync COMMAND library_sync_test)
    foreach(test_name library_query library_cache library_organization)
        add_executable(${test_name}_test "${CMAKE_SOURCE_DIR}/tests/${test_name}_test.cpp")
        target_link_libraries(${test_name}_test PRIVATE playden_library)
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()
endif()
