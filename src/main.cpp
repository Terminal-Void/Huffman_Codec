#include "huffman_codec.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

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
  [COMMANDS]
    c, compress    Compress any file into archive & sidecars
      ├─ Input     Source path (quotes auto-stripped)
      └─ Output    <file>.huf           Bitstream archive (HUF1)
                   <file>.config1.txt   Sidecar shape file
                   <file>.config2.txt   Sidecar leaves file

    x, extract     Decompress archive & restore original file
      ├─ Input     <file>.huf           Bitstream archive
      ├─ Requires  <file>.config1.txt   Sidecar shape file
      │            <file>.config2.txt   Sidecar leaves file
      └─ Output    <name>.decoded<ext>  Restored original file

    h, help        Show this help
    q, quit, exit  Exit

)";
    }

    std::string_view trim_ascii(const std::string_view text) noexcept {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    // 提示用户输入路径，读取并清理首尾空白及成对引号后返回
    fs::path read_path(const char *prompt) {
        std::cout << prompt;
        std::string line;
        if (!std::getline(std::cin, line)) {
            return {};
        }

        std::string_view path_view = trim_ascii(line);
        // 终端中拖拽文件输入路径时，Shell 往往会自动包裹成对单引号或双引号
        // 当首尾同时出现成对匹配的引号时剥离最外层引号
        // 防止误伤文件名本身合法包含的单个引号字符
        if (path_view.size() >= 2 &&
            ((path_view.front() == '"' && path_view.back() == '"') ||
             (path_view.front() == '\'' && path_view.back() == '\''))) {
            path_view.remove_prefix(1);
            path_view.remove_suffix(1);
        }

        return {path_view};
    }
} // namespace

int main() {
    print_banner();
    print_help();

    // 主循环
    std::string line;
    while (std::cout << "\n> " && std::getline(std::cin, line)) {
        const std::string_view choice = trim_ascii(line);
        if (choice.empty()) {
            continue; // 处理空行
        }
        try {
            if (choice == "c" || choice == "compress") {
                // 压缩
                const fs::path input_path = read_path("请输入原文件路径: ");
                if (!std::cin && input_path.empty()) {
                    break; // 遇到 EOF 时直接终止会话
                }
                const ArchivePaths paths = compress_file(input_path);
                std::cout << "压缩完成：\n"
                        << "  压缩文件    " << paths.archive.string() << '\n'
                        << "  树结构文件  " << paths.shape.string() << '\n'
                        << "  叶节点文件  " << paths.leaves.string() << '\n';
            } else if (choice == "x" || choice == "extract") {
                // 解压缩
                const fs::path archive_path = read_path("请输入压缩文件路径: ");
                if (!std::cin && archive_path.empty()) {
                    break;
                }
                const ArchivePaths paths = decompress_file(archive_path);
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
