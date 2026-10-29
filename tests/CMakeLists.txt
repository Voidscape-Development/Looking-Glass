# Unit tests for the parts of Looking Glass that do not depend on libobs: the
# layout data model, its JSON serialization, and the grid operations.
#
# This is a standalone project so it can run without an OBS build:
#   cmake -S tests -B build_tests
#   cmake --build build_tests
#   ctest --test-dir build_tests --output-on-failure

cmake_minimum_required(VERSION 3.16...3.30)

project(looking-glass-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)

enable_testing()

set(_lg_src "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_library(looking-glass-core STATIC "${_lg_src}/core/multiview-config.cpp" "${_lg_src}/core/grid-model.cpp")
target_include_directories(looking-glass-core PUBLIC "${_lg_src}/core")
target_link_libraries(looking-glass-core PUBLIC Qt6::Core Qt6::Gui)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(looking-glass-core PUBLIC -Wall -Wextra -Werror)
elseif(MSVC)
  target_compile_options(looking-glass-core PUBLIC /W3 /WX)
endif()

foreach(_test IN ITEMS test-grid-model test-serializer)
  add_executable(${_test} ${_test}.cpp)
  target_link_libraries(${_test} PRIVATE looking-glass-core Qt6::Test)
  add_test(NAME ${_test} COMMAND ${_test})
endforeach()
