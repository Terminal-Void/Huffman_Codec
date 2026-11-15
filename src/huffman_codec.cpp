#include "huffman_codec.h"
#include "bit_stream.h"

#include <array>
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
    // 承载哈夫曼子树的小根堆
    class MinHeap {
    private:
        std::vector<TreeHandle> items_;

        void sift_up(std::size_t index) {
            while (index > 0) {
                const std::size_t parent = (index - 1) / 2;
                if (items_[index]->weight() >= items_[parent]->weight()) {
                    break;
                }
                std::swap(items_[index], items_[parent]);
                index = parent;
            }
        }

        void sift_down(std::size_t index) {
            const std::size_t count = items_.size();
            while (true) {
                const std::size_t left = index * 2 + 1;
                if (left >= count) {
                    break;
                }

                std::size_t smallest = left;
                const std::size_t right = left + 1;
                if (right < count &&
                    items_[right]->weight() < items_[left]->weight()) {
                    smallest = right;
                }

                if (items_[index]->weight() <= items_[smallest]->weight()) {
                    break;
                }

                std::swap(items_[index], items_[smallest]);
                index = smallest;
            }
        }

    public:
        void push(TreeHandle tree) {
            if (tree == nullptr) {
                throw std::invalid_argument("不能向小根堆插入空子树");
            }
            items_.push_back(std::move(tree));
            sift_up(items_.size() - 1);
        }

        TreeHandle pop() {
            if (items_.empty()) {
                throw std::runtime_error("哈夫曼子树堆为空");
            }

            TreeHandle result = std::move(items_.front());
            if (items_.size() > 1) {
                items_.front() = std::move(items_.back());
                items_.pop_back();
                sift_down(0);
            } else {
                items_.pop_back();
            }
            return result;
        }

        [[nodiscard]] std::size_t size() const noexcept {
            return items_.size();
        }
    };

    // 每个堆元素作为一棵子树；合并子树时转移所有权至新根节点
    TreeHandle build_tree(const FrequencyTable &frequencies) {
        MinHeap heap; // 小根堆
        std::uint64_t total = 0;

        for (std::size_t i = 0; i < frequencies.size(); ++i) {
            const Frequency frequency = frequencies[i];
            if (frequency == 0) {
                continue;
            }

            if (frequency > static_cast<Frequency>(std::numeric_limits<int>::max())) {
                throw std::runtime_error("字符频率超过 int 范围");
            }

            total += frequency;
            Byte value = static_cast<Byte>(i);
            heap.push(std::make_unique<HuffTree<Byte> >(value, static_cast<int>(frequency)));
        }

        if (total == 0) {
            throw std::runtime_error("无法为空文件建立哈夫曼树");
        }

        while (heap.size() > 1) {
            TreeHandle left = heap.pop();
            TreeHandle right = heap.pop();

            if (left->weight() > std::numeric_limits<int>::max() - right->weight()) {
                throw std::runtime_error("哈夫曼树权重超过 int 范围");
            }
            // 合并子树，将子树的所有权转移至根
            heap.push(std::make_unique<HuffTree<Byte> >(std::move(left), std::move(right)));
        }

        return heap.pop();
    }

    // build_codes 的递归 helper
    void build_codes_recursive(Node *node,std::vector<bool> &path,CodeTable &codes) {
        if (node->isLeaf()) {
            const auto *leaf = dynamic_cast<LeafNode<Byte> *>(node);
            if (leaf == nullptr) {
                throw std::logic_error("叶节点类型与 isLeaf() 不一致");
            }
            auto &[bits] = codes[leaf->val()];
            bits = path;

            // 由于作业指定的 hufftree 实现
            // 导致需要特殊逻辑处理全文只有一种字符文件
            // 此处使用 0 编码位
            if (bits.empty()) {
                bits.push_back(false);
            }
            return;
        }

        const auto *internal = dynamic_cast<IntlNode<Byte> *>(node);
        if (internal == nullptr) {
            throw std::logic_error("内部节点类型与 isLeaf() 不一致");
        }

        path.push_back(false);
        build_codes_recursive(internal->left(), path, codes);
        path.pop_back();

        path.push_back(true);
        build_codes_recursive(internal->right(), path, codes);
        path.pop_back();
    }

    // 从哈夫曼树，建立字节对应哈夫曼编码的列表，保存在 codes
    void build_codes(const HuffTree<Byte>& tree, CodeTable &codes) {
        std::vector<bool> path;
        build_codes_recursive(tree.root(), path, codes);
    }

    std::uint64_t calculate_payload_bits(const FrequencyTable &frequencies, const CodeTable &codes) {
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
        std::uint64_t value = 0;
        unsigned buffered = 0;

        // 码表仍按位保存，这里分成不超过 56 位的片段交给位写入器。
        for (const bool bit: code.bits) {
            if (bit) {
                value |= std::uint64_t{1} << buffered;
            }
            ++buffered;

            if (buffered == 56) {
                writer.write_bits(value, buffered);
                value = 0;
                buffered = 0;
            }
        }

        if (buffered != 0) {
            writer.write_bits(value, buffered);
        }
    }

    std::size_t payload_byte_count(const std::uint64_t bit_count) {
        if (bit_count > std::numeric_limits<std::uint64_t>::max() - 7) {
            throw std::runtime_error("压缩位数超过可表示范围");
        }

        const std::uint64_t bytes = (bit_count + 7) / 8;
        if (bytes > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error("压缩载荷长度无法表示");
        }
        return static_cast<std::size_t>(bytes);
    }

    // 带 buffer 的逐字节处理器，传入自定义函数
    template<typename Function>
    void for_each_stream_byte(std::istream &input, Function &&function) {
        std::array<char, 64U * 1024U> buffer{};

        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize received = input.gcount();
            for (std::streamsize i = 0; i < received; ++i) {
                function(static_cast<Byte>(
                    static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)])
                ));
            }
        }

        if (input.bad() || (input.fail() && !input.eof())) {
            throw std::ios_base::failure("读取输入文件失败");
        }
    }

    void decode_payload(
        bitio::BitReader &reader,
        std::ostream &output,
        const HuffTree<Byte> &tree,
        const ArchiveHeader &header
    ) {
        Node *root = tree.root();
        std::uint64_t bits_read = 0;
        std::uint64_t bytes_written = 0;
        std::array<char, 64U * 1024U> output_buffer{};
        std::size_t output_used = 0;

        // 解码结果先写入固定大小的缓冲，再提交给目标文件。
        const auto flush_output = [&]() {
            if (output_used == 0) {
                return;
            }
            output.write(
                output_buffer.data(),
                static_cast<std::streamsize>(output_used)
            );
            if (!output) {
                throw std::ios_base::failure("写入解压文件失败");
            }
            output_used = 0;
        };

        const auto write_output_byte = [&](const Byte value) {
            output_buffer[output_used++] = static_cast<char>(value);
            if (output_used == output_buffer.size()) {
                flush_output();
            }
        };

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
                write_output_byte(value);
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
                write_output_byte(value);
                ++bytes_written;
            }
        }

        if (bits_read != header.payload_bits) {
            throw std::runtime_error("压缩数据包含多余编码位");
        }

        flush_output();
    }
} // namespace

// 压缩文件，并结构体返回压缩文件相关的路径组方便在主循环中打印具体信息
ArchivePaths compress_file(const fs::path &input_path) {
    // 检查文件合法性
    if (!fs::exists(input_path) || !fs::is_regular_file(input_path)) {
        throw std::runtime_error("输入路径不存在或不是普通文件");
    }
    // 根据输入文件路径生成路径组
    const ArchivePaths paths = ArchivePaths::from_input(input_path);

    // 打开输入文件流
    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw std::ios_base::failure("无法打开输入文件");
    }

    FrequencyTable frequencies{}; // 字节频率表
    std::uint64_t original_size = 0; // 文件字节数

    // 对每个字节累计频率和字节数
    for_each_stream_byte(input, [&](const Byte value) {
        ++frequencies[value];
        ++original_size;
    });

    if (original_size == 0) {
        throw std::runtime_error("不能压缩空文件");
    }

    // 根据频率表建树，并导出具体编码到编码表 codes
    TreeHandle tree = build_tree(frequencies);
    CodeTable codes{};
    build_codes(*tree, codes);

    // 文件头部信息
    const ArchiveHeader header{
        original_size,
        calculate_payload_bits(frequencies, codes)
    };

    // 创建树配置文件（leaves 使用二进制模式避免 Windows 下换行符转换）
    std::ofstream shape(paths.shape, std::ios::binary | std::ios::trunc);
    std::ofstream leaves(paths.leaves, std::ios::binary | std::ios::trunc);
    if (!shape || !leaves) {
        throw std::ios_base::failure("无法创建树配置文件");
    }

    // 写入树配置文件
    write_tree_config(*tree, shape, leaves);
    shape.flush();
    leaves.flush();
    if (!shape || !leaves) {
        throw std::ios_base::failure("写入树配置文件失败");
    }

    // 创建压缩文件
    std::ofstream archive(paths.archive, std::ios::binary | std::ios::trunc);
    if (!archive) {
        throw std::ios_base::failure("无法创建压缩文件");
    }
    // 写入压缩文件头
    header.write(archive);

    // 从头重新读取待压缩文件并编码
    input.clear();
    input.seekg(0, std::ios::beg);
    if (!input) {
        throw std::ios_base::failure("无法重新读取输入文件");
    }

    const std::size_t expected_payload_size = payload_byte_count(header.payload_bits);

    {
        bitio::BitWriter writer(archive);
        for_each_stream_byte(input, [&](const Byte value) {
            write_code(writer, codes[value]);
        });
        writer.flush();
        if (writer.written_bytes() != expected_payload_size) {
            throw std::runtime_error("编码位数与载荷长度不一致");
        }
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
        const std::size_t payload_size = payload_byte_count(header.payload_bits);
        bitio::BitReader reader(archive, payload_size);
        decode_payload(reader,decoded,*tree,header);
    }

    decoded.close();
    if (!decoded) {
        throw std::ios_base::failure("关闭解压文件失败");
    }

    return paths;
}
