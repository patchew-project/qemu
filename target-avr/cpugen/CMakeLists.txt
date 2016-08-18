cmake_minimum_required(VERSION 2.8)

project(cpugen)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -g3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")

set(Boost_USE_STATIC_LIBS   ON)
find_package(
    Boost 1.60.0
    REQUIRED
    COMPONENTS
        system
        regex)
#set(BUILD_SHARED_LIBS       OFF)
#set(BUILD_STATIC_LIBS       ON)
add_subdirectory(tinyxml2)
add_subdirectory(yaml-cpp)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../yaml-cpp/include
    ${Boost_INCLUDE_DIRS}
)

add_executable(
    cpugen
    src/cpugen.cpp
    src/utils.cpp
)

target_link_libraries(
    cpugen
    yaml-cpp
    tinyxml2
    ${Boost_LIBRARIES}
)
