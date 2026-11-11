#include "huffman_format.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <utility>

namespace fs = std::filesystem;
using Node = HuffNode<Byte>;

namespace {
    constexpr std::array<char, 4> kMagic = {'H', 'U', 'F', '1'};

    struct FileCloser {
        void operator()(FILE *file) const noexcept {
            if (file != nullptr) {
                std::fclose(file);
            }
        }
    };

    using FilePtr = std::unique_ptr<FILE, FileCloser>;

    FilePtr open_c_file(const fs::path &path, const char *mode) {
        FilePtr file(std::fopen(path.string().c_str(), mode));
        if (!file) {
            throw std::runtime_error(
                "无法打开文件：" + path.string()
            );
        }
        return file;
    }

    ArchivePaths make_paths_from_base(const fs::path &base) {
        fs::path archive = base;
        archive += ".huf";

        fs::path shape = base;
        shape += ".config1.txt";

        fs::path leaves = base;
        leaves += ".config2.txt";

        const std::string decoded_name =
                base.stem().string() + ".decoded" + base.extension().string();

        return {
            archive,
            shape,
            leaves,
            base.parent_path() / decoded_name
        };
    }

    void write_uint64(std::ostream &output, std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            output.put(static_cast<char>((value >> (i * 8)) & 0xffU));
        }

        if (!output) {
            throw std::ios_base::failure("写入压缩文件头失败");
        }
    }

    std::uint64_t read_uint64(std::istream &input) {
        std::uint64_t value = 0;

        for (int i = 0; i < 8; ++i) {
            const int byte = input.get();
            if (byte == EOF) {
                throw std::runtime_error("压缩文件头不完整");
            }
            value |= static_cast<std::uint64_t>(byte) << (i * 8);
        }

        return value;
    }

    std::unique_ptr<Node> parse_tree_node(FILE *shape, FILE *leaves) {
        const int marker = std::fgetc(shape);
        if (marker == EOF) {
            throw std::runtime_error("树结构文件提前结束");
        }

        if (marker == '0') {
            const int value = std::fgetc(leaves);
            if (value == EOF) {
                throw std::runtime_error("叶节点信息文件提前结束");
            }

            return std::make_unique<LeafNode<Byte> >(
                static_cast<Byte>(value),
                0
            );
        }

        if (marker != '1') {
            throw std::runtime_error("树结构文件包含非法标记");
        }

        auto left = parse_tree_node(shape, leaves);
        auto right = parse_tree_node(shape, leaves);

        return std::make_unique<IntlNode<Byte> >(
            std::move(left),
            std::move(right)
        );
    }

    void ensure_config_files_end(FILE *shape, FILE *leaves) {
        if (std::fgetc(shape) != EOF || std::ferror(shape)) {
            throw std::runtime_error("树结构文件包含多余内容");
        }

        if (std::fgetc(leaves) != EOF || std::ferror(leaves)) {
            throw std::runtime_error("叶节点信息文件包含多余内容");
        }
    }
} // namespace

ArchivePaths ArchivePaths::from_input(const fs::path &input) {
    return make_paths_from_base(input);
}

ArchivePaths ArchivePaths::from_archive(const fs::path &archive) {
    if (archive.extension() != ".huf") {
        throw std::runtime_error(
            "压缩文件扩展名必须是 .huf"
        );
    }

    fs::path base = archive;
    base.replace_extension();
    return make_paths_from_base(base);
}

void ArchiveHeader::write(std::ostream &output) const {
    output.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    write_uint64(output, original_size);
    write_uint64(output, payload_bits);
}

ArchiveHeader ArchiveHeader::read(std::istream &input) {
    std::array<char, 4> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));

    if (!input || magic != kMagic) {
        throw std::runtime_error("不是有效的 Huffman 压缩文件");
    }

    return {
        read_uint64(input),
        read_uint64(input)
    };
}

void write_tree_config(
    Node *node,
    std::ostream &shape,
    std::ostream &leaves
) {
    if (node->isLeaf()) {
        shape.put('0');
        auto *leaf = dynamic_cast<LeafNode<Byte> *>(node);
        if (leaf == nullptr) {
            throw std::logic_error("叶节点类型与 isLeaf() 不一致");
        }

        const auto value = static_cast<char>(leaf->val());
        leaves.put(value);
        return;
    }

    shape.put('1');
    auto *internal = dynamic_cast<IntlNode<Byte> *>(node);
    if (internal == nullptr) {
        throw std::logic_error("内部节点类型与 isLeaf() 不一致");
    }
    write_tree_config(internal->left(), shape, leaves);
    write_tree_config(internal->right(), shape, leaves);
}

TreeHandle GenHuffTreeByConfigFile(
    FILE *shape_file,
    FILE *leaves_file
) {
    if (shape_file == nullptr || leaves_file == nullptr) {
        throw std::invalid_argument("树配置文件句柄不能为空");
    }

    auto root = parse_tree_node(shape_file, leaves_file);
    ensure_config_files_end(shape_file, leaves_file);

    return std::make_unique<HuffTree<Byte> >(std::move(root));
}

TreeHandle load_tree_config(
    const fs::path &shape_path,
    const fs::path &leaves_path
) {
    FilePtr shape_file = open_c_file(shape_path, "r");
    FilePtr leaves_file = open_c_file(leaves_path, "r");

    return GenHuffTreeByConfigFile(
        shape_file.get(),
        leaves_file.get()
    );
}
