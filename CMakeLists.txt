cmake_minimum_required(VERSION 3.16)
# 2.0 development template. Not released.
project(OblivoneSort VERSION 2.0.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)

add_library(oblivone INTERFACE)
target_include_directories(oblivone INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(oblivone INTERFACE cxx_std_17)

if(MSVC)
    target_compile_options(oblivone INTERFACE /utf-8 /W4)
else()
    target_compile_options(oblivone INTERFACE -Wall -Wextra)
endif()

include(CTest)
find_package(Threads REQUIRED)

add_library(oblivone_parallel INTERFACE)
target_link_libraries(oblivone_parallel INTERFACE oblivone Threads::Threads)
set_target_properties(oblivone_parallel PROPERTIES EXPORT_NAME parallel)

# Experimental 2.0 advanced scheduler. Not installed yet.
add_library(oblivone_advanced INTERFACE)
target_link_libraries(oblivone_advanced INTERFACE oblivone Threads::Threads)

option(OBLIVONE_BUILD_BENCHMARKS "Build local benchmarks" OFF)
if(OBLIVONE_BUILD_BENCHMARKS)
    add_executable(bench_standard bench/bench_standard.cpp)
    target_link_libraries(bench_standard PRIVATE oblivone)

    add_executable(bench_cache bench/bench_cache.cpp)
    target_link_libraries(bench_cache PRIVATE oblivone)

    add_executable(bench_parallel bench/bench_parallel.cpp)
    target_link_libraries(bench_parallel PRIVATE oblivone_parallel)

    add_executable(bench_advanced bench/bench_advanced.cpp)
    target_link_libraries(bench_advanced PRIVATE oblivone_advanced oblivone_parallel)
endif()

if(BUILD_TESTING)
    add_executable(test_sort tests/test_sort.cpp)
    target_link_libraries(test_sort PRIVATE oblivone)
    add_test(NAME test_sort COMMAND test_sort)

    add_executable(fuzz_sort tests/fuzz_sort.cpp)
    target_link_libraries(fuzz_sort PRIVATE oblivone)
    add_test(NAME fuzz_sort COMMAND fuzz_sort 20000)

    add_executable(test_cache tests/test_cache.cpp)
    target_link_libraries(test_cache PRIVATE oblivone)
    add_test(NAME test_cache COMMAND test_cache)

    add_executable(fuzz_cache tests/fuzz_cache.cpp)
    target_link_libraries(fuzz_cache PRIVATE oblivone)
    add_test(NAME fuzz_cache COMMAND fuzz_cache 20000)

    add_executable(test_parallel tests/test_parallel.cpp)
    target_link_libraries(test_parallel PRIVATE oblivone_parallel)
    add_test(NAME test_parallel COMMAND test_parallel)

    add_executable(test_advanced tests/test_advanced.cpp)
    target_link_libraries(test_advanced PRIVATE oblivone_advanced)
    add_test(NAME test_advanced COMMAND test_advanced)

    add_executable(fuzz_advanced tests/fuzz_advanced.cpp)
    target_link_libraries(fuzz_advanced PRIVATE oblivone_advanced)
    add_test(NAME fuzz_advanced COMMAND fuzz_advanced 1000 1)
endif()

install(DIRECTORY include/ DESTINATION include
    PATTERN "advanced" EXCLUDE)
install(TARGETS oblivone oblivone_parallel EXPORT oblivoneTargets
    INCLUDES DESTINATION include)
install(EXPORT oblivoneTargets
    FILE oblivoneTargets.cmake
    NAMESPACE oblivone::
    DESTINATION lib/cmake/oblivone)

configure_package_config_file(
    cmake/oblivoneConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/oblivoneConfig.cmake"
    INSTALL_DESTINATION lib/cmake/oblivone)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/oblivoneConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/oblivoneConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/oblivoneConfigVersion.cmake"
    DESTINATION lib/cmake/oblivone)
