#include "huffman_codec.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
    void print_banner() {
        std::cout << R"(
  ██╗  ██╗██╗   ██╗███████╗███████╗███╗   ███╗ █████╗ ███╗   ██╗
  ██║  ██║██║   ██║██╔════╝██╔════╝████╗ ████║██╔══██╗████╗  ██║
  ███████║██║   ██║█████╗  █████╗  ██╔████╔██║███████║██╔██╗ ██║
  ██╔══██║██║   ██║██╔══╝  ██╔══╝  ██║╚██╔╝██║██╔══██║██║╚██╗██║
  ██║  ██║╚██████╔╝██║     ██║     ██║ ╚═╝ ██║██║  ██║██║ ╚████║
  ╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝     ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝
                 Huffman Coding Compression Tool
)" << '\n';
    }

    void print_help() {
        std::cout << R"(
[USAGE & WORKFLOW]
  c, compress:
    - Input  : Text file path (the assignment uses letter-based text files)
    - Output : Three files are created beside the input file.
               Their actual paths are printed after compression.

  x, extract:
    - Input  : Compressed archive path (.huf)
    - Rebuild: The tree is reconstructed from the two sidecar files.
    - Output : The restored file keeps the original extension.
               Its actual path is printed after extraction.

  h, help:
    - Print this help.

  q, quit, exit:
    - Terminate current session.
)";
    }

    fs::path read_path(const char *prompt) {
        std::cout << prompt;

        std::string text;
        std::getline(std::cin >> std::ws, text);
        return {text};
    }

    void print_compression_result(const ArchivePaths &paths) {
        std::cout
                << "压缩完成：\n"
                << "  压缩文件    " << paths.archive.string() << '\n'
                << "  树结构文件  " << paths.shape.string() << '\n'
                << "  叶节点文件  " << paths.leaves.string() << '\n';
    }

    void print_decompression_result(const ArchivePaths &paths) {
        std::cout
                << "解压完成：\n"
                << "  输出文件    " << paths.decoded.string() << '\n';
    }
} // namespace

int main() {
    print_banner();
    print_help();

    std::string choice;
    while (std::cout << "\n> " && std::getline(std::cin >> std::ws, choice)) {
        try {
            if (choice == "c" || choice == "compress") {
                const ArchivePaths paths = compress_file(read_path("请输入原文件路径："));
                print_compression_result(paths);
            } else if (choice == "x" || choice == "extract") {
                const ArchivePaths paths = decompress_file(read_path("请输入压缩文件路径："));
                print_decompression_result(paths);
            } else if (choice == "h" || choice == "help") {
                print_help();
            } else if (choice == "q" || choice == "quit" || choice == "exit"
            ) {
                break;
            } else {
                std::cout << "Invalid Command.\n";
                print_help();
            }
        } catch (const std::exception &error) {
            std::cerr << "操作失败：" << error.what() << '\n';
        }
    }
    std::cout << "Farewell." << std::endl;
    return 0;
}
