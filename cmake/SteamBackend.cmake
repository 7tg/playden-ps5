# Renderer-independent Steam protocol and verified HTTP transport targets.

add_library(playden_steam_protocol STATIC
    "${CMAKE_SOURCE_DIR}/src/steam/backend/proto_wire.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/backend/auth_messages.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/backend/cm_messages.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/backend/json.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam/backend/token_claims.cpp")
target_include_directories(playden_steam_protocol PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_steam_protocol PUBLIC cxx_std_17)
add_library(playden_steam_protobuf ALIAS playden_steam_protocol)

if(PLAYDEN_HOMEBREW_UI)
    if(PLAYDEN_PLATFORM_PS5)
        set(playden_steam_dependency_platform ps5)
    else()
        set(playden_steam_dependency_platform host)
    endif()

    add_library(playden_steam_auth STATIC
        "${CMAKE_SOURCE_DIR}/src/steam/backend/auth_client.cpp")
    target_include_directories(playden_steam_auth PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(playden_steam_auth PUBLIC cxx_std_17)
    target_link_libraries(playden_steam_auth PUBLIC
        playden_steam_http playden_steam_protocol playden_steam_core)
    if(PLAYDEN_PLATFORM_PS5)
        target_compile_options(playden_steam_auth PRIVATE -fno-exceptions -fno-rtti)
    endif()
    add_library(playden_steam_library STATIC
        "${CMAKE_SOURCE_DIR}/src/steam/backend/library_client.cpp")
    target_include_directories(playden_steam_library PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(playden_steam_library PUBLIC cxx_std_17)
    target_link_libraries(playden_steam_library PUBLIC
        playden_steam_http playden_steam_protocol playden_library_service)
    if(PLAYDEN_PLATFORM_PS5)
        target_compile_options(playden_steam_library PRIVATE -fno-exceptions -fno-rtti)
    endif()
    set(PLAYDEN_STEAM_PREFIX
        "${CMAKE_SOURCE_DIR}/probes/steam-network/.deps/${playden_steam_dependency_platform}"
        CACHE PATH "Pinned curl/Mbed TLS prefix prepared by the native Steam probe")

    foreach(required_path IN ITEMS
            "${PLAYDEN_STEAM_PREFIX}/include/curl/curl.h"
            "${PLAYDEN_STEAM_PREFIX}/lib/libcurl.a"
            "${PLAYDEN_STEAM_PREFIX}/cacert.pem")
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR
                "Pinned Steam dependency is missing: ${required_path}. "
                "Run probes/steam-network/prepare-${playden_steam_dependency_platform}-deps.sh")
        endif()
    endforeach()

    file(GLOB playden_curl_headers CONFIGURE_DEPENDS
        "${PLAYDEN_STEAM_PREFIX}/include/curl/*.h")
    string(JSON expected_header_count LENGTH "${playden_dependency_lock}"
        steam_network_probe public_headers)
    list(LENGTH playden_curl_headers actual_header_count)
    if(NOT actual_header_count EQUAL expected_header_count)
        message(FATAL_ERROR "Pinned curl public-header set mismatch")
    endif()
    foreach(header_path IN LISTS playden_curl_headers)
        file(RELATIVE_PATH header_key "${PLAYDEN_STEAM_PREFIX}/include" "${header_path}")
        string(JSON expected_hash GET "${playden_dependency_lock}"
            steam_network_probe public_headers "${header_key}")
        file(SHA256 "${header_path}" actual_hash)
        if(NOT actual_hash STREQUAL expected_hash)
            message(FATAL_ERROR "Pinned curl header hash mismatch: ${header_key}")
        endif()
    endforeach()
    set(playden_steam_archives)
    foreach(archive libcurl.a libeverest.a libmbedcrypto.a libmbedtls.a
                    libmbedx509.a libp256m.a)
        set(archive_path "${PLAYDEN_STEAM_PREFIX}/lib/${archive}")
        string(JSON expected_hash GET "${playden_dependency_lock}"
            steam_network_probe "${playden_steam_dependency_platform}_archives" "${archive}")
        file(SHA256 "${archive_path}" actual_hash)
        if(NOT actual_hash STREQUAL expected_hash)
            message(FATAL_ERROR "Pinned ${playden_steam_dependency_platform} archive hash mismatch: ${archive}")
        endif()
        list(APPEND playden_steam_archives "${archive_path}")
    endforeach()
    string(JSON expected_ca_hash GET "${playden_dependency_lock}"
        steam_network_probe ca_bundle sha256)
    file(SHA256 "${PLAYDEN_STEAM_PREFIX}/cacert.pem" actual_ca_hash)
    if(NOT actual_ca_hash STREQUAL expected_ca_hash)
        message(FATAL_ERROR "Pinned Steam CA bundle hash mismatch")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/dependencies.lock.json" ${playden_curl_headers}
        ${playden_steam_archives} "${PLAYDEN_STEAM_PREFIX}/cacert.pem")

    add_library(playden_curl_8_22 STATIC IMPORTED GLOBAL)
    set_target_properties(playden_curl_8_22 PROPERTIES
        IMPORTED_LOCATION "${PLAYDEN_STEAM_PREFIX}/lib/libcurl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${PLAYDEN_STEAM_PREFIX}/include"
        INTERFACE_COMPILE_DEFINITIONS CURL_STATICLIB
        INTERFACE_LINK_LIBRARIES
            "${PLAYDEN_STEAM_PREFIX}/lib/libmbedtls.a;${PLAYDEN_STEAM_PREFIX}/lib/libmbedx509.a;${PLAYDEN_STEAM_PREFIX}/lib/libmbedcrypto.a;${PLAYDEN_STEAM_PREFIX}/lib/libeverest.a;${PLAYDEN_STEAM_PREFIX}/lib/libp256m.a;Threads::Threads")
    if(APPLE AND NOT PLAYDEN_PLATFORM_PS5)
        set_property(TARGET playden_curl_8_22 APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "-framework SystemConfiguration" "-framework CoreFoundation"
            "-framework CoreServices")
    endif()

    add_library(playden_steam_http STATIC
        "${CMAKE_SOURCE_DIR}/src/steam/backend/http_client.cpp"
        "${CMAKE_SOURCE_DIR}/src/steam/backend/ws_client.cpp"
        "${CMAKE_SOURCE_DIR}/src/steam/backend/native_network.cpp")
    target_include_directories(playden_steam_http PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(playden_steam_http PUBLIC cxx_std_17)
    target_link_libraries(playden_steam_http PUBLIC playden_curl_8_22 Threads::Threads)
    if(PLAYDEN_PLATFORM_PS5)
        target_compile_definitions(playden_steam_http PRIVATE PLAYDEN_PLATFORM_PS5=1)
        target_compile_options(playden_steam_http PRIVATE -fno-exceptions -fno-rtti)
    endif()

    # Authenticated CM session for the CM-only library facts (acquisition dates).
    add_library(playden_steam_cm STATIC
        "${CMAKE_SOURCE_DIR}/src/steam/backend/cm_client.cpp")
    target_include_directories(playden_steam_cm PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(playden_steam_cm PUBLIC cxx_std_17)
    target_link_libraries(playden_steam_cm PUBLIC
        playden_steam_http playden_steam_protocol playden_steam_cm_codec playden_steam_core)
    if(PLAYDEN_PLATFORM_PS5)
        target_compile_options(playden_steam_cm PRIVATE -fno-exceptions -fno-rtti)
    endif()

    set(PLAYDEN_STEAM_CA_BUNDLE "${PLAYDEN_STEAM_PREFIX}/cacert.pem"
        CACHE INTERNAL "Verified Steam HTTPS trust bundle")
    set(PLAYDEN_STEAM_LINK_ARCHIVES
        "${PLAYDEN_STEAM_PREFIX}/lib/libcurl.a"
        "${PLAYDEN_STEAM_PREFIX}/lib/libmbedtls.a"
        "${PLAYDEN_STEAM_PREFIX}/lib/libmbedx509.a"
        "${PLAYDEN_STEAM_PREFIX}/lib/libmbedcrypto.a"
        "${PLAYDEN_STEAM_PREFIX}/lib/libeverest.a"
        "${PLAYDEN_STEAM_PREFIX}/lib/libp256m.a"
        CACHE INTERNAL "Verified native Steam transport archives")

    set(playden_staged_ca "${CMAKE_BINARY_DIR}/trust/cacert.pem")
    add_custom_command(OUTPUT "${playden_staged_ca}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/trust"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PLAYDEN_STEAM_CA_BUNDLE}" "${playden_staged_ca}"
        DEPENDS "${PLAYDEN_STEAM_CA_BUNDLE}" VERBATIM)
    add_custom_target(playden_steam_trust ALL DEPENDS "${playden_staged_ca}")
endif()

function(playden_register_steam_backend_tests)
    if(NOT BUILD_TESTING OR PLAYDEN_PLATFORM_PS5)
        return()
    endif()

    foreach(test_name proto_wire auth_messages)
        add_executable(${test_name}_test "${CMAKE_SOURCE_DIR}/tests/${test_name}_test.cpp")
        target_link_libraries(${test_name}_test PRIVATE playden_steam_protocol)
    endforeach()
    add_test(NAME proto_wire COMMAND proto_wire_test)
    add_test(NAME auth_messages COMMAND auth_messages_test
        "${CMAKE_SOURCE_DIR}/fixtures/steam/auth")

    foreach(test_name steam_json token_claims)
        add_executable(${test_name}_test "${CMAKE_SOURCE_DIR}/tests/${test_name}_test.cpp")
        target_link_libraries(${test_name}_test PRIVATE playden_steam_protocol)
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()

    foreach(test_name session_store steam_auth)
        add_executable(${test_name}_test "${CMAKE_SOURCE_DIR}/tests/${test_name}_test.cpp")
        target_link_libraries(${test_name}_test PRIVATE playden_steam_core)
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()

    if(TARGET playden_steam_http)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_executable(http_client_test "${CMAKE_SOURCE_DIR}/tests/http_client_test.cpp")
        target_link_libraries(http_client_test PRIVATE playden_steam_http)
        add_test(NAME http_client
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/http_fixture_server.py"
                "$<TARGET_FILE:http_client_test>" "${CMAKE_SOURCE_DIR}/tests/fixtures/http")
        set_tests_properties(http_client PROPERTIES TIMEOUT 15)

        add_executable(auth_client_test "${CMAKE_SOURCE_DIR}/tests/auth_client_test.cpp")
        target_link_libraries(auth_client_test PRIVATE playden_steam_auth)
        add_test(NAME auth_client COMMAND auth_client_test)

        foreach(test_name cm_messages cm_client)
            add_executable(${test_name}_test "${CMAKE_SOURCE_DIR}/tests/${test_name}_test.cpp")
            target_link_libraries(${test_name}_test PRIVATE playden_steam_cm)
            add_test(NAME ${test_name} COMMAND ${test_name}_test "${CMAKE_SOURCE_DIR}/fixtures/steam/cm")
        endforeach()

        add_executable(library_client_test "${CMAKE_SOURCE_DIR}/tests/library_client_test.cpp")
        target_link_libraries(library_client_test PRIVATE playden_steam_library)
        add_test(NAME library_client COMMAND library_client_test
            "${CMAKE_SOURCE_DIR}/fixtures/steam/library")
    endif()
endfunction()
