cmake_minimum_required(VERSION 3.10)
project(longshun C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3)
else()
    add_compile_options(-Wall -Wextra)
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_executable(longshun ${SOURCES})
target_include_directories(longshun PRIVATE include)

if(WIN32)
    target_link_libraries(longshun ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(longshun Threads::Threads)
endif()
