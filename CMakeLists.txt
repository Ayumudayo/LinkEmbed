cmake_minimum_required(VERSION 3.22)
project(LinkEmbed)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# find_package will use the vcpkg toolchain automatically
find_package(dpp CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(lexbor CONFIG QUIET)
find_package(Threads REQUIRED)

add_executable(LinkEmbed
    src/main.cpp
    src/core/LinkEmbedHandler.cpp
    src/core/JobScheduler.cpp
    src/network/HTMLFetcher.cpp
    src/parser/MetadataParser.cpp
    src/cache/MetadataCache.cpp
    src/utils/Logger.cpp
    src/utils/UrlUtil.cpp
    src/utils/EmbedBuilder.cpp
    config/Config.cpp
    src/utils/ThreadPool.cpp
    src/utils/RateLimiter.cpp
)

target_include_directories(LinkEmbed PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/config
)

target_link_libraries(LinkEmbed PRIVATE
    dpp::dpp
    CURL::libcurl
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(WIN32)
    target_link_libraries(LinkEmbed PRIVATE ws2_32)
endif()

# Note: lexbor vcpkg port may not provide a CMake config target.
# Try CONFIG first; if not found, manually locate include/lib.
if (lexbor_FOUND)
    target_link_libraries(LinkEmbed PRIVATE lexbor::lexbor)
else()
    find_path(LEXBOR_INCLUDE_DIR NAMES lexbor/html/html.h)
    find_library(LEXBOR_LIBRARY NAMES lexbor)
    if (NOT LEXBOR_INCLUDE_DIR OR NOT LEXBOR_LIBRARY)
        message(FATAL_ERROR "Could not find lexbor. Install via vcpkg or set CMAKE_PREFIX_PATH/lexbor_DIR")
    endif()
    target_include_directories(LinkEmbed PRIVATE ${LEXBOR_INCLUDE_DIR})
    target_link_libraries(LinkEmbed PRIVATE ${LEXBOR_LIBRARY})
endif()

# ===================================================================
# Compiler flags for Release and Debug builds
# ===================================================================
if(MSVC)
    # MSVC (Visual Studio) compiler settings
    target_compile_options(LinkEmbed PRIVATE
        $<$<CONFIG:DEBUG>:/Zi /Od /RTC1>
        $<$<CONFIG:RELEASE>:/O2 /Oi /Gy /GL>
    )
    # Interpret source files as UTF-8
    target_compile_options(LinkEmbed PRIVATE /utf-8)
    target_compile_definitions(LinkEmbed PRIVATE
        $<$<CONFIG:RELEASE>:NDEBUG>
    )
else()
    # Settings for GCC/Clang and other compilers
    target_compile_options(LinkEmbed PRIVATE
        $<$<CONFIG:DEBUG>:-g>
        $<$<CONFIG:RELEASE>:-O3 -DNDEBUG>
    )
endif()

# ===================================================================
# CTest for build verification
# ===================================================================
enable_testing()

# A simple smoke test to ensure the executable runs without crashing.
# The test passes if the program returns an exit code of 0.
add_test(NAME RunTest COMMAND LinkEmbed)
