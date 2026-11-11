#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iosfwd>
#include <memory>

#include "huffman_types.h"
#include "hufftree.h"

using TreeHandle = std::unique_ptr<HuffTree<Byte> >;

struct ArchivePaths {
    std::filesystem::path archive;
    std::filesystem::path shape;
    std::filesystem::path leaves;
    std::filesystem::path decoded;

    static ArchivePaths from_input(
        const std::filesystem::path &input
    );

    static ArchivePaths from_archive(
        const std::filesystem::path &archive
    );
};

struct ArchiveHeader {
    std::uint64_t original_size = 0;
    std::uint64_t payload_bits = 0;

    void write(std::ostream &output) const;

    static ArchiveHeader read(std::istream &input);
};

void write_tree_config(
    HuffNode<Byte> *root,
    std::ostream &shape,
    std::ostream &leaves
);

TreeHandle load_tree_config(
    const std::filesystem::path &shape_path,
    const std::filesystem::path &leaves_path
);

TreeHandle GenHuffTreeByConfigFile(
    FILE *shape_file,
    FILE *leaves_file
);
