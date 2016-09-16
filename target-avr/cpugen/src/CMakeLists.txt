#
# CPUGEN
#
# Copyright (c) 2016 Michael Rolnik
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

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
set(BUILD_SHARED_LIBS       OFF)
set(BUILD_STATIC_LIBS       ON)
add_subdirectory(../tinyxml2    ${CMAKE_CURRENT_BINARY_DIR}/tinyxml2)
add_subdirectory(../yaml-cpp    ${CMAKE_CURRENT_BINARY_DIR}/yaml-cpp)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../yaml-cpp/include
    ${Boost_INCLUDE_DIRS}
)

add_executable(
    cpugen
    cpugen.cpp
    utils.cpp
)

target_link_libraries(
    cpugen
    yaml-cpp
    tinyxml2_static
    ${Boost_LIBRARIES}
)
