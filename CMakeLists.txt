cmake_minimum_required(VERSION 3.21)

project(ComsPlus VERSION 1.0.0 LANGUAGES CXX)

option(COMSPLUS_BUILD_TESTS "Build ComsPlus unit tests" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(COMSPLUS_CORE_SOURCES
    src/core/ChatCore.cpp
)

add_library(comsplus_core STATIC ${COMSPLUS_CORE_SOURCES})
target_include_directories(comsplus_core PUBLIC src/core)

if(COMSPLUS_BUILD_TESTS)
    add_executable(comsplus_core_tests test/chat_core_test.cpp)
    target_link_libraries(comsplus_core_tests PRIVATE comsplus_core)
endif()
