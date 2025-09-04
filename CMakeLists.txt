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
find_package(Catch2 CONFIG QUIET)
find_package(PkgConfig QUIET)

add_executable(LinkEmbed
    src/main.cpp
    src/core/LinkEmbedHandler.cpp
    src/core/JobScheduler.cpp
    src/network/HTMLFetcher.cpp
    src/parser/MetadataParser.cpp
    src/parser/DefaultMetadataParser.cpp
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
    # Some distributions/packages expose different CMake target names
    if (TARGET lexbor::lexbor)
        target_link_libraries(LinkEmbed PRIVATE lexbor::lexbor)
    elseif (TARGET lexbor)
        target_link_libraries(LinkEmbed PRIVATE lexbor)
    elseif (DEFINED LEXBOR_LIBRARIES OR DEFINED LEXBOR_LIBRARY)
        # Rarely, the config provides only variables
        if (DEFINED LEXBOR_INCLUDE_DIRS)
            target_include_directories(LinkEmbed PRIVATE ${LEXBOR_INCLUDE_DIRS})
        endif()
        if (DEFINED LEXBOR_LIBRARIES)
            target_link_libraries(LinkEmbed PRIVATE ${LEXBOR_LIBRARIES})
        else()
            target_link_libraries(LinkEmbed PRIVATE ${LEXBOR_LIBRARY})
        endif()
    else()
        message(WARNING "lexbor was found but no usable CMake target was provided. Falling back to pkg-config/manual discovery.")
        set(lexbor_FOUND FALSE)
    endif()
endif()

if (NOT lexbor_FOUND)
    # Fallback: try pkg-config (vcpkg often provides a .pc file)
    if (PkgConfig_FOUND)
        pkg_check_modules(PC_LEXBOR QUIET lexbor)
    endif()
    if (PC_LEXBOR_FOUND)
        target_include_directories(LinkEmbed PRIVATE ${PC_LEXBOR_INCLUDE_DIRS})
        target_link_libraries(LinkEmbed PRIVATE ${PC_LEXBOR_LIBRARIES})
    else()
    # Help CMake find lexbor installed via vcpkg even if no CMake config is provided
    # Use vcpkg toolchain variables when available
    find_path(LEXBOR_INCLUDE_DIR NAMES lexbor/html/html.h
        HINTS
            ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
            ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/include
    )
    find_library(LEXBOR_LIBRARY NAMES lexbor
        HINTS
            ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib
            ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib
    )
    if (NOT LEXBOR_INCLUDE_DIR OR NOT LEXBOR_LIBRARY)
        message(FATAL_ERROR "Could not find lexbor. Install via vcpkg or set CMAKE_PREFIX_PATH/lexbor_DIR")
    endif()
    target_include_directories(LinkEmbed PRIVATE ${LEXBOR_INCLUDE_DIR})
    target_link_libraries(LinkEmbed PRIVATE ${LEXBOR_LIBRARY})
    endif()
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

# Tests
if (Catch2_FOUND)
    add_subdirectory(tests)
else()
    message(STATUS "Catch2 not found; tests will be skipped. Install via vcpkg to enable.")
endif()
