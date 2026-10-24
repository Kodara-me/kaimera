cmake_minimum_required(VERSION 3.24)

project(Kaimera VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt Multimedia plays and decodes the MP4 animations the service returns.
find_package(Qt6 6.5 REQUIRED COMPONENTS Multimedia Network WebSockets Widgets)

set(NUKLEUS_SOURCE_DIR "" CACHE PATH "Path to a Nukleus source checkout")
if(NOT NUKLEUS_SOURCE_DIR)
    set(NUKLEUS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../nukleus")
endif()
set(NUKLEUS_SDK_INSTALL OFF CACHE BOOL "Install and export the Nukleus SDK" FORCE)
include(FetchContent)
if(EXISTS "${NUKLEUS_SOURCE_DIR}/src/sdk/CMakeLists.txt")
    add_subdirectory("${NUKLEUS_SOURCE_DIR}/src/sdk" "${CMAKE_BINARY_DIR}/nukleus-sdk" EXCLUDE_FROM_ALL)
else()
    # Without a sibling checkout: an installed NukleusSdk package, otherwise the pinned tag's SDK.
    FetchContent_Declare(NukleusSdk
        GIT_REPOSITORY https://github.com/jstmichel/nukleus.git
        GIT_TAG v0.3.0
        SOURCE_SUBDIR src/sdk
        FIND_PACKAGE_ARGS 0.3 CONFIG)
    FetchContent_MakeAvailable(NukleusSdk)
endif()

FetchContent_Declare(Ardoise
    GIT_REPOSITORY https://github.com/jstmichel/ardoise.git
    GIT_TAG v0.1.0
    FIND_PACKAGE_ARGS 0.1 CONFIG)
FetchContent_MakeAvailable(Ardoise)

include(CTest)

add_subdirectory(src)
add_subdirectory(tests)
