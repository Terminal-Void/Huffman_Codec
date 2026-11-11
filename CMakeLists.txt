cmake_minimum_required(VERSION 4.2)
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
