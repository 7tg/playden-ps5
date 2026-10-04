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
    "${CMAKE_SOURCE_DIR}/src/steam/install_service.cpp")
target_include_directories(playden_steam_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_steam_core PUBLIC cxx_std_17)
target_link_libraries(playden_steam_core PUBLIC playden_storage Threads::Threads)

add_library(playden_library_service STATIC
    "${CMAKE_SOURCE_DIR}/src/library/service.cpp")
target_include_directories(playden_library_service PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_library_service PUBLIC cxx_std_17)
target_link_libraries(playden_library_service PUBLIC playden_steam_core)

add_library(playden_preferences STATIC "${CMAKE_SOURCE_DIR}/src/app/preferences.cpp")
target_include_directories(playden_preferences PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_preferences PUBLIC cxx_std_17)
target_link_libraries(playden_preferences PUBLIC playden_storage)

add_library(playden_persistence_service STATIC "${CMAKE_SOURCE_DIR}/src/app/persistence_service.cpp")
target_include_directories(playden_persistence_service PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_persistence_service PUBLIC cxx_std_17)
target_link_libraries(playden_persistence_service PUBLIC playden_preferences playden_library Threads::Threads)

if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
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
