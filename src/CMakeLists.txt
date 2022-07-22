cmake_minimum_required(VERSION 3.13.4)
project(includeguardian)

SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

# Download automatically, you can also just copy the conan.cmake file
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
    file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/release/0.17/conan.cmake"
                "${CMAKE_BINARY_DIR}/conan.cmake")
endif()


include(${CMAKE_BINARY_DIR}/conan.cmake)

conan_cmake_run(CONANFILE conanfile.txt
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

find_package(LLVM CONFIG REQUIRED)
message("found LLVM ${LLVM_CMAKE_DIR}")

find_package(Clang CONFIG REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions)
add_definitions(${LLVM_DEFINITIONS})

add_library(
    common
    STATIC
    cost.hpp cost.cpp
    graph.hpp graph.cpp
    build_graph.hpp build_graph.cpp
    dot_graph.hpp dot_graph.cpp
    find_expensive_files.hpp find_expensive_files.cpp
    find_expensive_headers.hpp find_expensive_headers.cpp
    find_expensive_includes.hpp find_expensive_includes.cpp
    list_included_files.hpp list_included_files.cpp
    find_unnecessary_sources.hpp find_unnecessary_sources.cpp
    get_total_cost.hpp get_total_cost.cpp
    reachability_graph.hpp
)
target_link_libraries(common CONAN_PKG::boost)
target_link_libraries(common clangTooling)

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs Support)

target_include_directories(common PUBLIC ${LLVM_INCLUDE_DIRS})

# Link against LLVM libraries
target_link_libraries(common ${llvm_libs})

if(WIN32)
    target_compile_options(common PRIVATE /EHsc /FS)
endif()

find_package(GTest)
include(GoogleTest)
add_executable(
    tests
    build_graph.test.cpp
    dot_graph.test.cpp
    find_expensive_files.test.cpp
    find_expensive_headers.test.cpp
    find_expensive_includes.test.cpp
    list_included_files.test.cpp
    find_unnecessary_sources.test.cpp
    get_total_cost.test.cpp
    reachability_graph.test.cpp
)
target_link_libraries(tests CONAN_PKG::gtest)
target_link_libraries(tests common)
gtest_add_tests(TARGET      tests
                TEST_SUFFIX .noArgs
                TEST_LIST   noArgsTests
)

add_executable(
    includeguardian
    main.m.cpp
)
target_link_libraries(includeguardian common)