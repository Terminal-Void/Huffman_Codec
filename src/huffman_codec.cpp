#include "huffman_codec.h"
#include "bit_stream.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Node = HuffNode<Byte>;

namespace {
    class MinHeap {
    private:
        std::vector<TreeHandle> items_;

        struct Compare {
            bool operator()(
                const TreeHandle &left,
                const TreeHandle &right
            ) const {
                return left->weight() > right->weight();
            }
        };

    public:
        void push(TreeHandle tree) {
            items_.push_back(std::move(tree));
            std::push_heap(items_.begin(), items_.end(), Compare{});
        }

        TreeHandle pop_min() {
            if (items_.empty()) {
                throw std::runtime_error("哈夫曼堆为空");
            }

            std::pop_heap(items_.begin(), items_.end(), Compare{});
            TreeHandle result = std::move(items_.back());
            items_.pop_back();
            return result;
        }

        [[nodiscard]] std::size_t size() const noexcept {
            return items_.size();
        }
    };

    TreeHandle build_tree(const FrequencyTable &frequencies) {
        MinHeap heap;
        std::uint64_t total = 0;

        for (std::size_t i = 0; i < frequencies.size(); ++i) {
            const Frequency frequency = frequencies[i];
            if (frequency == 0) {
                continue;
            }

            if (frequency > static_cast<Frequency>(
                    std::numeric_limits<int>::max())) {
                throw std::runtime_error("字符频率超过 HuffTree 的 int 范围");
            }

            total += frequency;
            Byte value = static_cast<Byte>(i);
            heap.push(std::make_unique<HuffTree<Byte> >(
                value,
                static_cast<int>(frequency)
            ));
        }

        if (total == 0) {
            throw std::runtime_error("不能为没有字符的文件建立哈夫曼树");
        }

        while (heap.size() > 1) {
            TreeHandle left = heap.pop_min();
            TreeHandle right = heap.pop_min();

            if (left->weight() >
                std::numeric_limits<int>::max() - right->weight()) {
                throw std::runtime_error("哈夫曼树权重超过 int 范围");
            }

            heap.push(std::make_unique<HuffTree<Byte> >(
                std::move(left),
                std::move(right)
            ));
        }

        return heap.pop_min();
    }

    void build_codes(
        Node *node,
        std::vector<bool> &path,
        CodeTable &codes
    ) {
        if (node->isLeaf()) {
            auto *leaf = dynamic_cast<LeafNode<Byte> *>(node);
            if (leaf == nullptr) {
                throw std::logic_error("叶节点类型与 isLeaf() 不一致");
            }
            Codeword &code = codes[leaf->val()];
            code.bits = path;

            // 单字符文件也使用一个明确的 0 编码位。
            if (code.bits.empty()) {
                code.bits.push_back(false);
            }
            return;
        }

        auto *internal = dynamic_cast<IntlNode<Byte> *>(node);
        if (internal == nullptr) {
            throw std::logic_error("内部节点类型与 isLeaf() 不一致");
        }

        path.push_back(false);
        build_codes(internal->left(), path, codes);
        path.pop_back();

        path.push_back(true);
        build_codes(internal->right(), path, codes);
        path.pop_back();
    }

    std::uint64_t calculate_payload_bits(
        const FrequencyTable &frequencies,
        const CodeTable &codes
    ) {
        std::uint64_t result = 0;

        for (std::size_t i = 0; i < frequencies.size(); ++i) {
            if (frequencies[i] == 0) {
                continue;
            }

            const auto length = static_cast<std::uint64_t>(
                codes[i].bits.size()
            );

            if (length != 0 && frequencies[i] >
                std::numeric_limits<std::uint64_t>::max() / length) {
                throw std::runtime_error("压缩位数溢出");
            }

            const std::uint64_t contribution = frequencies[i] * length;
            if (result >
                std::numeric_limits<std::uint64_t>::max() - contribution) {
                throw std::runtime_error("压缩位数溢出");
            }

            result += contribution;
        }

        return result;
    }

    void write_code(bitio::BitWriter &writer, const Codeword &code) {
        for (const bool bit: code.bits) {
            writer.write_bit(bit);
        }
    }

    void decode_payload(
        bitio::BitReader &reader,
        std::ostream &output,
        Node *root,
        const ArchiveHeader &header
    ) {
        std::uint64_t bits_read = 0;
        std::uint64_t bytes_written = 0;

        const auto read_payload_bit = [&]() {
            if (bits_read >= header.payload_bits) {
                throw std::runtime_error("压缩数据提前结束");
            }
            ++bits_read;
            return reader.read_bit();
        };

        if (root->isLeaf()) {
            auto *leaf = dynamic_cast<LeafNode<Byte> *>(root);
            if (leaf == nullptr) {
                throw std::logic_error("根节点类型与 isLeaf() 不一致");
            }
            const Byte value = leaf->val();

            while (bytes_written < header.original_size) {
                if (read_payload_bit()) {
                    throw std::runtime_error("单节点哈夫曼树的编码无效");
                }
                output.put(static_cast<char>(value));
                ++bytes_written;
            }
        } else {
            while (bytes_written < header.original_size) {
                Node *node = root;

                while (!node->isLeaf()) {
                    const bool bit = read_payload_bit();
                    auto *internal = dynamic_cast<IntlNode<Byte> *>(node);
                    if (internal == nullptr) {
                        throw std::logic_error(
                            "内部节点类型与 isLeaf() 不一致"
                        );
                    }
                    node = bit ? internal->right() : internal->left();

                    if (node == nullptr) {
                        throw std::runtime_error("哈夫曼树结构无效");
                    }
                }

                auto *leaf = dynamic_cast<LeafNode<Byte> *>(node);
                if (leaf == nullptr) {
                    throw std::logic_error(
                        "叶节点类型与 isLeaf() 不一致"
                    );
                }

                const Byte value = leaf->val();
                output.put(static_cast<char>(value));
                ++bytes_written;
            }
        }

        if (bits_read != header.payload_bits) {
            throw std::runtime_error("压缩数据包含多余编码位");
        }

        if (!output) {
            throw std::ios_base::failure("写入解压文件失败");
        }
    }
} // namespace

ArchivePaths compress_file(const fs::path &input_path) {
    if (!fs::exists(input_path) || !fs::is_regular_file(input_path)) {
        throw std::runtime_error("输入路径不是普通文件");
    }

    const ArchivePaths paths = ArchivePaths::from_input(input_path);

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw std::ios_base::failure("无法打开输入文件");
    }

    FrequencyTable frequencies{};
    std::uint64_t original_size = 0;
    char ch;

    while (input.get(ch)) {
        ++frequencies[static_cast<unsigned char>(ch)];
        ++original_size;
    }

    if (input.bad()) {
        throw std::ios_base::failure("读取输入文件失败");
    }
    if (original_size == 0) {
        throw std::runtime_error("不能压缩空文件");
    }

    TreeHandle tree = build_tree(frequencies);
    CodeTable codes{};
    std::vector<bool> path;
    build_codes(tree->root(), path, codes);

    const ArchiveHeader header{
        original_size,
        calculate_payload_bits(frequencies, codes)
    };

    std::ofstream shape(paths.shape, std::ios::trunc);
    std::ofstream leaves(paths.leaves, std::ios::trunc);
    if (!shape || !leaves) {
        throw std::ios_base::failure("无法创建树配置文件");
    }

    write_tree_config(tree->root(), shape, leaves);
    shape.flush();
    leaves.flush();
    if (!shape || !leaves) {
        throw std::ios_base::failure("写入树配置文件失败");
    }

    std::ofstream archive(
        paths.archive,
        std::ios::binary | std::ios::trunc
    );
    if (!archive) {
        throw std::ios_base::failure("无法创建压缩文件");
    }

    header.write(archive);

    input.clear();
    input.seekg(0, std::ios::beg);
    if (!input) {
        throw std::ios_base::failure("无法重新读取输入文件");
    }

    {
        bitio::BitWriter writer(archive);
        while (input.get(ch)) {
            write_code(
                writer,
                codes[static_cast<unsigned char>(ch)]
            );
        }
        if (input.bad()) {
            throw std::ios_base::failure("编码时读取输入文件失败");
        }
        writer.flush();
    }

    archive.close();
    shape.close();
    leaves.close();

    if (!archive || !shape || !leaves) {
        throw std::ios_base::failure("关闭压缩输出文件失败");
    }

    return paths;
}

ArchivePaths decompress_file(const fs::path &archive_path) {
    if (!fs::exists(archive_path) ||
        !fs::is_regular_file(archive_path)) {
        throw std::runtime_error("压缩路径不是普通文件");
    }

    const ArchivePaths paths =
            ArchivePaths::from_archive(archive_path);

    std::ifstream archive(archive_path, std::ios::binary);
    if (!archive) {
        throw std::ios_base::failure("无法打开压缩文件");
    }

    const ArchiveHeader header = ArchiveHeader::read(archive);
    if (header.original_size == 0 || header.payload_bits == 0) {
        throw std::runtime_error("压缩文件头中的长度无效");
    }

    TreeHandle tree = load_tree_config(
        paths.shape,
        paths.leaves
    );

    std::ofstream decoded(
        paths.decoded,
        std::ios::binary | std::ios::trunc
    );
    if (!decoded) {
        throw std::ios_base::failure("无法创建解压文件");
    }

    {
        bitio::BitReader reader(archive);
        decode_payload(
            reader,
            decoded,
            tree->root(),
            header
        );
    }

    decoded.close();
    if (!decoded) {
        throw std::ios_base::failure("关闭解压文件失败");
    }

    return paths;
}
