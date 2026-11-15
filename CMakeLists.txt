cmake_minimum_required(VERSION 3.20)
project(Project2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Project2
        src/main.cpp
        src/huffman_codec.cpp
        src/huffman_format.cpp
        include/bit_stream.h
        include/huffnode.h
        include/hufftree.h
        include/huffman_types.h
        include/huffman_format.h
        include/huffman_codec.h
)

target_include_directories(Project2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_testing()

add_executable(Project2Tests
        src/huffman_codec.cpp
        src/huffman_format.cpp
        third_party/catch2/catch_amalgamated.cpp
        third_party/catch2/catch_amalgamated.hpp
        tests/test_helpers.h
        tests/test_huff_tree.cpp
        tests/test_bit_stream.cpp
        tests/test_huffman_format.cpp
        tests/test_huffman_codec.cpp
)

target_include_directories(Project2Tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(Project2Tests SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/catch2
)

target_compile_definitions(Project2Tests PRIVATE
        PROJECT2_CLI_PATH="$<TARGET_FILE:Project2>"
)

add_dependencies(Project2Tests Project2)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Catch)
catch_discover_tests(Project2Tests)
