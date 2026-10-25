cmake_minimum_required(VERSION 3.16)
project(OblivoneSort VERSION 0.1.0 LANGUAGES CXX)

add_library(oblivone INTERFACE)
target_include_directories(oblivone INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(oblivone INTERFACE cxx_std_17)

if(MSVC)
  add_compile_options(/utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(test_sort tests/test_sort.cpp)
  target_link_libraries(test_sort PRIVATE oblivone)
  add_test(NAME test_sort COMMAND test_sort)
endif()

add_executable(bench_sort bench/bench_sort.cpp)
target_link_libraries(bench_sort PRIVATE oblivone)
