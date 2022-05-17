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

add_executable(
    includeguardian
    main.m.cpp
    reachability_graph.hpp reachability_graph.cpp
    print_graph_factory.hpp print_graph_factory.cpp
    find_expensive_includes.hpp find_expensive_includes.cpp
)
target_link_libraries(includeguardian CONAN_PKG::boost)
target_link_libraries(includeguardian clangTooling)

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs Support)

target_include_directories(includeguardian PRIVATE ${LLVM_INCLUDE_DIRS})

# Link against LLVM libraries
target_link_libraries(includeguardian ${llvm_libs})

if(WIN32)
    target_compile_options(includeguardian PRIVATE /EHsc /FS)
endif()