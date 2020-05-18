cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

project(BeTus VERSION 0.1 LANGUAGES CXX)

# include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
# conan_basic_setup(TARGETS)
set(CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_VERSION 17)
set(CMAKE_CXX_VERSION_REQUIRED True)

set(CMAKE_EXPORT_COMPILE_COMMANDS On)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system uuid)

add_executable(betusd src/server.cpp src/tus_manager.cpp src/files_manager.cpp)
target_compile_options(betusd PRIVATE -Wall -Werror)
target_compile_options(betusd PUBLIC -std=c++17)
target_include_directories(betusd PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(betusd Threads::Threads Boost::Boost)

enable_testing()
find_package(Catch2 REQUIRED)
add_executable(betest test/files_manager_test.cpp test/tus_manager_test.cpp src/files_manager.cpp src/tus_manager.cpp)
target_compile_options(betest PRIVATE -Wall -Werror)
target_compile_options(betest PUBLIC -std=c++17)
target_include_directories(betest PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(betest PRIVATE Catch2::Catch2 Boost::Boost)

include(CTest)
include(Catch)
catch_discover_tests(betest)

