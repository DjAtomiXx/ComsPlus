cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

project(ComsPlus VERSION 1.0.0 LANGUAGES CXX)

option(COMSPLUS_BUILD_TESTS "Build ComsPlus unit tests" OFF)

set(COMSPLUS_CORE_SOURCES
    src/core/ChatCore.cpp
)

add_library(comsplus_core STATIC ${COMSPLUS_CORE_SOURCES})
target_include_directories(comsplus_core PUBLIC src/core)

if(COMSPLUS_BUILD_TESTS)
    add_executable(comsplus_core_tests test/chat_core_test.cpp)
    target_link_libraries(comsplus_core_tests PRIVATE comsplus_core)
endif()

if(DEFINED ENV{GEODE_SDK} AND NOT COMSPLUS_BUILD_TESTS)
    if(ANDROID)
        add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include type_traits>")
    endif()

    file(GLOB_RECURSE COMSPLUS_MOD_SOURCES CONFIGURE_DEPENDS
        src/*.cpp
    )
    list(REMOVE_ITEM COMSPLUS_MOD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/ChatCore.cpp")

    add_library(${PROJECT_NAME} SHARED
        ${COMSPLUS_MOD_SOURCES}
        ${COMSPLUS_CORE_SOURCES}
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        src/core
        src/geode
    )

    set(GLOBED_DEP_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/geode-deps/dankmeme.globed2/include")
    set(GLOBED_VENDOR_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/globed2/include")
    if(EXISTS "${GLOBED_DEP_INCLUDE}")
        target_include_directories(${PROJECT_NAME} PRIVATE "${GLOBED_DEP_INCLUDE}")
        target_compile_definitions(${PROJECT_NAME} PRIVATE COMSPLUS_HAS_GLOBED_HEADERS=1)
    elseif(EXISTS "${GLOBED_VENDOR_INCLUDE}/globed/soft-link/API.hpp")
        target_include_directories(${PROJECT_NAME} PRIVATE "${GLOBED_VENDOR_INCLUDE}")
        target_compile_definitions(${PROJECT_NAME} PRIVATE COMSPLUS_HAS_GLOBED_HEADERS=1)
    endif()

    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
    add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
    setup_geode_mod(${PROJECT_NAME})
endif()
