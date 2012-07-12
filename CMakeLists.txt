cmake_minimum_required (VERSION 2.6)
project (Sockets)

include_directories ("${CMAKE_CURRENT_SOURCE_DIR}/include")

# Set the compiler to use C++11
add_definitions("-std=c++11")
add_definitions("-Wall")
add_definitions("-pedantic")
add_definitions("-O3")

add_library(Sockets "${CMAKE_CURRENT_SOURCE_DIR}/lib/Socket.cpp")
