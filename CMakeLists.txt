cmake_minimum_required(VERSION 3.26)

project(priority_strand)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE include)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED)
if (!Boost_FOUND)
    message("Not found boost")
endif ()

option(${PROJECT_NAME}_ENABLE_BENCHMARKING "Enable benchmarking" ON)

find_package(benchmark REQUIRED)

if(${PROJECT_NAME}_ENABLE_BENCHMARKING)
    find_package(benchmark QUIET REQUIRED)

    add_executable(benchmark_${PROJECT_NAME} bm_priority_strand.cpp )

    target_link_libraries(benchmark_${PROJECT_NAME} benchmark::benchmark ${PROJECT_NAME})
    set_target_properties(benchmark_${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
endif()
