cmake_minimum_required(VERSION 3.28)
project(KairoECS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(KAIRO_ECS_BUILD_TESTS "Build KairoECS tests" ${PROJECT_IS_TOP_LEVEL})
option(KAIRO_ECS_BUILD_BENCHMARK "Build KairoECS deterministic benchmark" ${PROJECT_IS_TOP_LEVEL})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

add_library(KairoECS)
add_library(Kairo::ECS ALIAS KairoECS)
target_sources(KairoECS PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    Entity.cppm SparseSet.cppm Registry.cppm KairoECS.cppm)
set_property(TARGET KairoECS PROPERTY COMPILE_WARNING_AS_ERROR ON)

if(KAIRO_ECS_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
        GIT_SHALLOW TRUE
        GIT_SUBMODULES "")
    FetchContent_MakeAvailable(Catch2)
    add_executable(KairoECSTests tests/ECSTests.cpp)
    target_link_libraries(KairoECSTests PRIVATE KairoECS Catch2::Catch2WithMain)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(KairoECSTests PRIVATE -Wno-c2y-extensions)
    endif()
    set_property(TARGET KairoECSTests PROPERTY COMPILE_WARNING_AS_ERROR ON)
    add_test(NAME KairoECSTests COMMAND KairoECSTests)
endif()


if(KAIRO_ECS_BUILD_BENCHMARK)
    add_executable(KairoECSBenchmark benchmarks/ECSBenchmark.cpp)
    target_link_libraries(KairoECSBenchmark PRIVATE KairoECS)
endif()
