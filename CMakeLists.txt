cmake_minimum_required(VERSION 3.26)
project(call_stack)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED)

if (Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    add_executable(CallingStack main.cpp call_stack.hpp a.hpp priority_strand.hpp)
endif ()
