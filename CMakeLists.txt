cmake_minimum_required(VERSION 3.10)
project(src)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

find_package(Boost REQUIRED)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINATIONS})
llvm_map_components_to_libnames(LLVM_LIBS all)

add_executable(src main.cc)
target_link_libraries(src ${LLVM_LIBS} LLVM)
