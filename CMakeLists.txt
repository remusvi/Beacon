cmake_minimum_required(VERSION 3.16)
project(Beacon2_0 LANGUAGES CXX)

# Quiet policy CMP0167 warning and use CMake's modern Boost target discovery
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /EHsc)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
    include_directories(SYSTEM /opt/homebrew/include /usr/local/include)
    link_directories(/opt/homebrew/lib /usr/local/lib)
endif()

set(CMAKE_USE_LIBIDN2 OFF CACHE BOOL "Disable IDN2 support for curl" FORCE)

include(FetchContent)

# Boost process v1 depends on Boost::filesystem
find_package(Boost REQUIRED COMPONENTS filesystem)

FetchContent_Declare(
        webview
        GIT_REPOSITORY https://github.com/webview/webview
        GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(webview)

FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json
        GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)

set(BUILD_SHARED_LIBS OFF)
set(CPR_BUILD_TESTS OFF CACHE INTERNAL "")

FetchContent_Declare(
        cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG 1.11.1
)
FetchContent_MakeAvailable(cpr)

add_executable(Beacon2_0 main.cpp
        core/servers/servers.h
        core/servers/add_server.cpp
        core/servers/delete_server.cpp
        core/containers/container.h
        core/utils/utils.cpp
        core/containers/containers.cpp
        core/servers/get_servers.cpp
        core/servers/start_server.cpp
        core/servers/player_manager.cpp
        core/p2p/p2p_manager.h
        core/p2p/p2p_manager.cpp
)

target_include_directories(Beacon2_0 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/interface2/dist")

# 2. Link Boost::process (this supplies the v2 process/error symbol binaries)
target_link_libraries(Beacon2_0 PRIVATE
    webview::core
    nlohmann_json::nlohmann_json
    cpr::cpr
    Boost::headers
    Boost::filesystem
)
if(TARGET Boost::process)
    target_link_libraries(Beacon2_0 PRIVATE Boost::process)
endif()

# Asset Compilation Step
set(GENERATED_UI_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/interface2/dist/index_html.h")

find_program(NPM_EXECUTABLE NAMES npm.cmd npm)
if(NOT NPM_EXECUTABLE)
    set(NPM_EXECUTABLE npm)
endif()

add_custom_command(
        OUTPUT "${GENERATED_UI_HEADER}"
        COMMAND ${NPM_EXECUTABLE} run build --prefix "${CMAKE_CURRENT_SOURCE_DIR}/interface2"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building user interface production assets..."
)

target_sources(Beacon2_0 PRIVATE "${GENERATED_UI_HEADER}")
