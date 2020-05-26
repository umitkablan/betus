cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

project(BeTus VERSION 0.1 LANGUAGES CXX)

# include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
# conan_basic_setup(TARGETS)
set(CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_BUILD_TYPE MATCHES Debug)
    include(CodeCoverage)
endif()

set(CMAKE_CXX_VERSION 17)
set(CMAKE_CXX_VERSION_REQUIRED True)

set(CMAKE_EXPORT_COMPILE_COMMANDS On)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system uuid)

add_executable(betusd src/server.cpp src/tus_manager.cpp src/files_manager.cpp)
target_compile_options(betusd PRIVATE -Wall -Wextra -Werror)
target_compile_options(betusd PUBLIC -std=c++17)
target_include_directories(betusd PUBLIC ${CMAKE_CURRENT_LIST_DIR})
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(betusd PRIVATE -fno-omit-frame-pointer -fsanitize=address)
    target_link_libraries(betusd -lasan)
endif()
target_link_libraries(betusd Threads::Threads Boost::Boost)

enable_testing()
find_package(Catch2 REQUIRED)
add_executable(betest test/files_manager_test.cpp test/tus_manager_test.cpp src/files_manager.cpp src/tus_manager.cpp)
target_compile_options(betest PRIVATE -Wall -Wextra -Werror)
target_compile_options(betest PUBLIC -std=c++17)
target_include_directories(betest PUBLIC ${CMAKE_CURRENT_LIST_DIR})
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(betest PRIVATE -fno-omit-frame-pointer -fsanitize=address)
    target_link_libraries(betest -lasan)
endif()
target_link_libraries(betest Catch2::Catch2 Boost::Boost)

if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_BUILD_TYPE MATCHES Debug)
    setup_target_for_coverage_gcovr_html(
        NAME betest_coverage
        EXECUTABLE betest
        DEPENDENCIES betest
        EXCLUDE "${CMAKE_CURRENT_LIST_DIR}/test/*")
    target_compile_options(betest PUBLIC -g -O0 --coverage -fprofile-arcs -ftest-coverage)
    append_coverage_compiler_flags(-O0)
endif()

include(CTest)
include(Catch)
catch_discover_tests(betest)

