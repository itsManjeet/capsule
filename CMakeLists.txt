cmake_minimum_required(VERSION 3.20)
project(capsule
        VERSION "0.1"
        DESCRIPTION "Capsule Programming Language"
        LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(ExternalProject)
ExternalProject_Add(tinycc
        GIT_REPOSITORY "git://repo.or.cz/tinycc.git"
        GIT_TAG "mob"
        GIT_SHALLOW ON
        GIT_PROGRESS ON

        CONFIGURE_COMMAND "<SOURCE_DIR>/configure" --prefix=<INSTALL_DIR> --enable-static
)
ExternalProject_Get_Property(tinycc INSTALL_DIR)
add_library(tcc INTERFACE)
target_include_directories(tcc INTERFACE "${INSTALL_DIR}/include")
target_link_libraries(tcc INTERFACE "${INSTALL_DIR}/lib/libtcc.a")

#add_compile_definitions(-DDEBUG_GC)
#add_compile_definitions(-DSTRESS_GC)

include_directories(include)

add_subdirectory(src)
add_subdirectory(bin)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_Shared
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/
)