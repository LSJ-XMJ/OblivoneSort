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

add_executable(bench_small_sort bench/bench_small_sort.cpp)
target_link_libraries(bench_small_sort PRIVATE oblivone)

add_executable(bench_small_top bench/bench_small_top.cpp)
target_link_libraries(bench_small_top PRIVATE oblivone)

add_executable(bench_leaf bench/bench_leaf.cpp)
target_link_libraries(bench_leaf PRIVATE oblivone)

add_executable(bench_grid bench/bench_grid.cpp)
target_link_libraries(bench_grid PRIVATE oblivone)

add_executable(bench_standard bench/bench_standard.cpp)
target_link_libraries(bench_standard PRIVATE oblivone)

add_executable(bench_tiny bench/bench_tiny.cpp)
target_link_libraries(bench_tiny PRIVATE oblivone)

add_executable(bench_sort2 bench/bench_sort2.cpp)
target_link_libraries(bench_sort2 PRIVATE oblivone)

add_executable(bench_nokey bench/bench_nokey.cpp)
target_link_libraries(bench_nokey PRIVATE oblivone)

add_executable(bench_pair bench/bench_pair.cpp)
target_link_libraries(bench_pair PRIVATE oblivone)

add_executable(bench_leaf_less bench/bench_leaf_less.cpp)
target_link_libraries(bench_leaf_less PRIVATE oblivone)

add_executable(bench_key_modes bench/bench_key_modes.cpp)
target_link_libraries(bench_key_modes PRIVATE oblivone)

add_executable(bench_double_map bench/bench_double_map.cpp)
target_link_libraries(bench_double_map PRIVATE oblivone)

add_executable(bench_sub_bucket bench/bench_sub_bucket.cpp)
target_link_libraries(bench_sub_bucket PRIVATE oblivone)

add_executable(bench_offset_tables bench/bench_offset_tables.cpp)
target_link_libraries(bench_offset_tables PRIVATE oblivone)

add_executable(bench_offset_first bench/bench_offset_first.cpp)
target_link_libraries(bench_offset_first PRIVATE oblivone)

add_executable(bench_bscan bench/bench_bscan.cpp)
target_link_libraries(bench_bscan PRIVATE oblivone)

add_executable(bench_bcap_real bench/bench_bcap_real.cpp)
target_link_libraries(bench_bcap_real PRIVATE oblivone)

add_executable(bench_mean_center bench/bench_mean_center.cpp)
target_link_libraries(bench_mean_center PRIVATE oblivone)

find_package(Threads REQUIRED)
add_executable(bench_parallel bench/bench_parallel.cpp)
target_link_libraries(bench_parallel PRIVATE oblivone Threads::Threads)

add_executable(bench_parallel_pool bench/bench_parallel_pool.cpp)
target_link_libraries(bench_parallel_pool PRIVATE oblivone Threads::Threads)

add_executable(bench_offset_hybrid bench/bench_offset_hybrid.cpp)
target_link_libraries(bench_offset_hybrid PRIVATE oblivone)

add_executable(bench_known bench/bench_known.cpp)
target_link_libraries(bench_known PRIVATE oblivone)

find_path(PDQSORT_INCLUDE_DIR pdqsort.h
  HINTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdqsort
        ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/pdqsort
        $ENV{PDQSORT_ROOT})
find_path(SKA_SORT_INCLUDE_DIR ska_sort.hpp
  HINTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ska_sort
        ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/ska_sort
        $ENV{SKA_SORT_ROOT})
find_path(TIMSORT_INCLUDE_DIR gfx/timsort.hpp
  HINTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-TimSort/include
        ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/cpp-TimSort/include
        $ENV{TIMSORT_ROOT})
find_path(BOOST_INCLUDE_DIR boost/sort/spreadsort/spreadsort.hpp
  HINTS $ENV{BOOST_ROOT} $ENV{BOOST_ROOT}/include
        "D:/VSC_TOOL/boost-1.89.0")

if(PDQSORT_INCLUDE_DIR)
  target_include_directories(bench_known PRIVATE ${PDQSORT_INCLUDE_DIR})
endif()
if(SKA_SORT_INCLUDE_DIR)
  target_include_directories(bench_known PRIVATE ${SKA_SORT_INCLUDE_DIR})
endif()
if(TIMSORT_INCLUDE_DIR)
  target_include_directories(bench_known PRIVATE ${TIMSORT_INCLUDE_DIR})
  target_compile_features(bench_known PRIVATE cxx_std_20)

  # TEST ONLY: hybrid requires the third-party TimSort header.
  add_executable(bench_hybrid bench/bench_hybrid.cpp)
  target_link_libraries(bench_hybrid PRIVATE oblivone)
  target_include_directories(bench_hybrid PRIVATE ${TIMSORT_INCLUDE_DIR})
  target_compile_features(bench_hybrid PRIVATE cxx_std_20)
endif()
if(BOOST_INCLUDE_DIR)
  target_include_directories(bench_known PRIVATE ${BOOST_INCLUDE_DIR})
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  target_include_directories(bench_sort PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  target_include_directories(bench_matrix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()
