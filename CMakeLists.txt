cmake_minimum_required(VERSION 3.10)
project(src)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

find_package(Boost REQUIRED)

add_executable(src main.cc)