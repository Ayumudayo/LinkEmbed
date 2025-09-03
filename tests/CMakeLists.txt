cmake_minimum_required(VERSION 3.22)

add_executable(linkembed_tests
    test_url_embed.cpp
    test_core_utils.cpp
)

target_include_directories(linkembed_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_sources(linkembed_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/utils/UrlUtil.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/EmbedBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/RateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/cache/MetadataCache.cpp
)

target_link_libraries(linkembed_tests PRIVATE
    Catch2::Catch2WithMain
    dpp::dpp
)

add_test(NAME linkembed_tests COMMAND linkembed_tests --reporter console --success --durations yes)

# Optional: produce JUnit XML for CI systems
add_test(NAME linkembed_tests_junit COMMAND linkembed_tests --reporter junit --durations yes --out ${CMAKE_BINARY_DIR}/test-results.xml)
