cmake_minimum_required(VERSION 3.20)
project(capsule
        VERSION "0.1"
        DESCRIPTION "Capsule Programming Language"
        LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#add_compile_definitions(-DDEBUG_GC)
#add_compile_definitions(-DSTRESS_GC)

include_directories(include)

add_subdirectory(src)
add_subdirectory(bin)
add_subdirectory(modules)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_Shared
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/
)