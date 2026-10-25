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

  add_executable(fuzz_sort tests/fuzz_sort.cpp)
  target_link_libraries(fuzz_sort PRIVATE oblivone)
  add_test(NAME fuzz_sort COMMAND fuzz_sort 5000)
endif()

add_executable(bench_sort bench/bench_sort.cpp)
target_link_libraries(bench_sort PRIVATE oblivone)

add_executable(bench_matrix bench/bench_matrix.cpp)
target_link_libraries(bench_matrix PRIVATE oblivone)

add_executable(bench_small bench/bench_small.cpp)
target_link_libraries(bench_small PRIVATE oblivone)

add_executable(bench_leaf bench/bench_leaf.cpp)
target_link_libraries(bench_leaf PRIVATE oblivone)

add_executable(bench_grid bench/bench_grid.cpp)
target_link_libraries(bench_grid PRIVATE oblivone)

add_executable(bench_standard bench/bench_standard.cpp)
target_link_libraries(bench_standard PRIVATE oblivone)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  target_include_directories(bench_sort PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  target_include_directories(bench_matrix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()
