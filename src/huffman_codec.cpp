#include "huffman_codec.h"
#include "bit_stream.h"

#include <algorithm>
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
    class MinHeap {
    private:
        std::vector<TreeHandle> items_;

        struct Compare {
            bool operator()(const TreeHandle &left, const TreeHandle &right) const {
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

            if (frequency > static_cast<Frequency>(std::numeric_limits<int>::max())) {
                throw std::runtime_error("字符频率超过 HuffTree 的 int 范围");
            }

            total += frequency;
            Byte value = static_cast<Byte>(i);
            heap.push(std::make_unique<HuffTree<Byte> >(value, static_cast<int>(frequency)));
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

            heap.push(std::make_unique<HuffTree<Byte> >(std::move(left), std::move(right)));
        }

        return heap.pop_min();
    }

    void build_codes(Node *node, std::vector<bool> &path, CodeTable &codes) {
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
        Node *root,
        const ArchiveHeader &header
    ) {
        std::uint64_t bits_read = 0;
        std::uint64_t bytes_written = 0;
        std::array<char, 64U * 1024U> output_buffer{};
        std::size_t output_used = 0;

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
    for_each_stream_byte(input, [&](const Byte value) {
        ++frequencies[value];
        ++original_size;
    });
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

    const std::size_t expected_payload_size =
            payload_byte_count(header.payload_bits);
    std::vector<Byte> payload;
    constexpr std::size_t kPayloadBufferSize = 64U * 1024U;
    payload.reserve(kPayloadBufferSize + 4U);
    std::size_t emitted_payload_size = 0;

    {
        const auto drain_payload = [&]() {
            if (payload.empty()) {
                return;
            }

            archive.write(
                reinterpret_cast<const char *>(payload.data()),
                static_cast<std::streamsize>(payload.size())
            );
            if (!archive) {
                throw std::ios_base::failure("写入压缩数据失败");
            }

            emitted_payload_size += payload.size();
            payload.clear();
        };
        bitio::BitWriter writer(payload);
        for_each_stream_byte(input, [&](const Byte value) {
            write_code(writer, codes[value]);
            if (payload.size() >= kPayloadBufferSize) {
                drain_payload();
            }
        });
        writer.flush();
        drain_payload();
    }

    if (emitted_payload_size != expected_payload_size) {
        throw std::runtime_error("编码位数与载荷长度不一致");
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
