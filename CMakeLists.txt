cmake_minimum_required(VERSION 3.20)

project(resona-deck-recommend VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(GNUInstallDirs)

set(SEKAI_DECK_RECOMMEND_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/sekai-deck-recommend-cpp"
    CACHE PATH "Path to the sekai-deck-recommend-cpp source tree")

if (NOT EXISTS "${SEKAI_DECK_RECOMMEND_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "sekai-deck-recommend-cpp is missing; run git submodule update --init && git -C sekai-deck-recommend-cpp submodule update --init 3rdparty/json")
endif()

set(SEKAI_DECK_RECOMMEND_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(SEKAI_DECK_RECOMMEND_BUILD_NATIVE ON CACHE BOOL "" FORCE)
add_subdirectory(
    "${SEKAI_DECK_RECOMMEND_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/sekai-deck-recommend-cpp"
)

FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
    URL_HASH SHA256=a0567bcd6c3fe5cef1b329b96245119047f876b49e06cc129a36a7a8dffe173e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
if (NOT WIN32)
    set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(cpp_httplib)

FetchContent_Declare(
    tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tomlplusplus)

find_package(Threads REQUIRED)

add_executable(resona-deck-recommend
    src/config.cpp
    src/deck_recommend_service.cpp
    src/fetch.cpp
    src/main.cpp
)

target_include_directories(resona-deck-recommend PRIVATE src)
target_link_libraries(resona-deck-recommend PRIVATE
    SekaiDeckRecommend::Core
    httplib::httplib
    tomlplusplus::tomlplusplus
    Threads::Threads
)

if (WIN32)
    target_link_libraries(resona-deck-recommend PRIVATE winhttp)
    target_compile_definitions(resona-deck-recommend PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if (MSVC)
    target_compile_options(resona-deck-recommend PRIVATE /W4 /MP /utf-8)
else()
    target_compile_options(resona-deck-recommend PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion
    )
endif()

set_property(TARGET resona-deck-recommend
    PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

add_custom_command(TARGET resona-deck-recommend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SEKAI_DECK_RECOMMEND_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:resona-deck-recommend>/data"
)

install(TARGETS resona-deck-recommend
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${SEKAI_DECK_RECOMMEND_SOURCE_DIR}/data/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/resona-deck-recommend")
install(FILES config.example.toml
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/resona-deck-recommend")
