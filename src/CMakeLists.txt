cmake_minimum_required(VERSION 3.5)
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

add_executable(
    includeguardian
    main.m.cpp
    reachability_graph.hpp reachability_graph.cpp
)
target_link_libraries(includeguardian CONAN_PKG::boost)
if(WIN32)
    target_compile_definitions(includeguardian PRIVATE _ITERATOR_DEBUG_LEVEL=0)
endif()