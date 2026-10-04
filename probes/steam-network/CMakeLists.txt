cmake_minimum_required(VERSION 3.24)
project(playden_steam_network_probe LANGUAGES CXX)

find_package(CURL 8.22.0 EXACT CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(playden-steam-network-probe-lib STATIC main.cpp)
target_compile_definitions(playden-steam-network-probe-lib PRIVATE
    PLAYDEN_STEAM_NETWORK_PROBE_NO_MAIN=1)
target_compile_features(playden-steam-network-probe-lib PUBLIC cxx_std_20)
target_compile_options(playden-steam-network-probe-lib PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(playden-steam-network-probe-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(playden-steam-network-probe-lib PUBLIC CURL::libcurl Threads::Threads)

if(NOT PLAYDEN_STEAM_NETWORK_PROBE_LIBRARY_ONLY)
    add_executable(playden-steam-network-probe main.cpp)
    target_compile_features(playden-steam-network-probe PRIVATE cxx_std_20)
    target_compile_options(playden-steam-network-probe PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(playden-steam-network-probe PRIVATE CURL::libcurl Threads::Threads)
endif()
