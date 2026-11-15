#include "huffman_codec.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
    // 打印 Banner
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

    // 打印帮助
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

    // 提示用户输入路径，读取并转换为 fs::path 后返回
    fs::path read_path(const char *prompt) {
        std::cout << prompt;
        std::string text;
        std::getline(std::cin >> std::ws, text);
        return {text};
    }
} // namespace

int main() {
    print_banner();
    print_help();

    // 主循环
    std::string choice;
    while (std::cout << "\n> " && std::getline(std::cin >> std::ws, choice)) {
        try {
            if (choice == "c" || choice == "compress") {
                // 压缩
                const ArchivePaths paths = compress_file(read_path("请输入原文件路径: "));
                std::cout << "压缩完成：\n"
                        << "  压缩文件    " << paths.archive.string() << '\n'
                        << "  树结构文件  " << paths.shape.string() << '\n'
                        << "  叶节点文件  " << paths.leaves.string() << '\n';
            } else if (choice == "x" || choice == "extract") {
                // 解压缩
                const ArchivePaths paths = decompress_file(read_path("请输入压缩文件路径: "));
                std::cout << "解压完成：\n" << "  输出文件    " << paths.decoded.string() << '\n';
            } else if (choice == "h" || choice == "help") {
                // 帮助
                print_help();
            } else if (choice == "q" || choice == "quit" || choice == "exit") {
                // 退出
                break;
            } else {
                // 非法命令
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
