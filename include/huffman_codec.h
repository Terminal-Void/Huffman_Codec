#pragma once

#include <filesystem>

#include "huffman_format.h"

ArchivePaths compress_file(const std::filesystem::path &input_path);

ArchivePaths decompress_file(const std::filesystem::path &archive_path);
