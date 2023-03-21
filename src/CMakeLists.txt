cmake_minimum_required(VERSION 3.13.4)
project(includeguardian)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

find_package(GTest CONFIG REQUIRED)

set(Boost_USE_STATIC_LIBS_ON)
find_package(Boost REQUIRED COMPONENTS system graph serialization)

find_package(LLVM CONFIG REQUIRED)
message (STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message (STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include(HandleLLVMOptions)
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs Core Support)

find_package(Clang CONFIG REQUIRED)

find_path(TERMCOLOR_INCLUDE_DIRS "termcolor/termcolor.hpp")

###################
# common          #
###################
add_library(
    common
    SHARED
    cost.hpp cost.cpp
    graph.hpp graph.cpp
    build_graph.hpp build_graph.cpp
    dfs.hpp
    dot_graph.hpp dot_graph.cpp
    find_expensive_files.hpp find_expensive_files.cpp
    find_expensive_headers.hpp find_expensive_headers.cpp
    find_expensive_includes.hpp find_expensive_includes.cpp
    includeguardian.hpp includeguardian.cpp
    list_included_files.hpp list_included_files.cpp
    find_unnecessary_sources.hpp find_unnecessary_sources.cpp
    find_unused_components.hpp find_unused_components.cpp
    get_total_cost.hpp get_total_cost.cpp
    reachability_graph.hpp
    recommend_precompiled.hpp recommend_precompiled.cpp
    topological_order.hpp topological_order.cpp
)

target_compile_definitions(common PUBLIC ${LLVM_DEFINITIONS_LIST})

target_include_directories(common PUBLIC ${Boost_INCLUDE_DIRS})
target_include_directories(common PUBLIC ${LLVM_INCLUDE_DIRS})
target_include_directories(common PUBLIC ${TERMCOLOR_INCLUDE_DIRS})

target_link_libraries(common PUBLIC GTest::gtest GTest::gmock)
target_link_libraries(common PUBLIC Boost::system Boost::graph Boost::headers Boost::serialization)
target_link_libraries(common PUBLIC ${llvm_libs})
target_link_libraries(common PUBLIC clangTooling)

target_precompile_headers(common
  PUBLIC
    <boost/graph/adjacency_list.hpp>
    <boost/units/io.hpp>
    <boost/units/quantity.hpp>
    <gtest/gtest.h>
    <filesystem>
    <execution>
)

if(WIN32)
    target_compile_options(common PRIVATE /EHsc /FS)
endif()

###################
# tests           #
###################
add_executable(
    tests
    analysis_test_fixtures.hpp analysis_test_fixtures.cpp
    build_graph.test.cpp
    dot_graph.test.cpp
    find_expensive_files.test.cpp
    find_expensive_headers.test.cpp
    find_expensive_includes.test.cpp
    list_included_files.test.cpp
    find_unnecessary_sources.test.cpp
    find_unused_components.test.cpp
    get_total_cost.test.cpp
    matchers.hpp
    reachability_graph.test.cpp
    topological_order.test.cpp
    serialize_graph.test.cpp
)
target_precompile_headers(tests REUSE_FROM common)
if(WIN32)
    target_compile_options(tests PRIVATE /EHsc /FS)
endif()

target_link_libraries(tests PRIVATE GTest::gtest_main GTest::gmock_main)
target_link_libraries(tests PUBLIC common)

###################
# includeguardian #
###################
add_executable(
    includeguardian
    main.m.cpp
)
if(WIN32)
    target_compile_options(includeguardian PRIVATE /EHsc /FS)
endif()
target_link_libraries(includeguardian PUBLIC common)

###################
# benchmarks      #
###################
add_executable(
    benchmarks
    benchmarks.m.cpp
)
if(WIN32)
    target_compile_options(benchmarks PRIVATE /EHsc /FS)
endif()
find_package(benchmark CONFIG REQUIRED)
target_link_libraries(benchmarks PUBLIC common benchmark::benchmark)
