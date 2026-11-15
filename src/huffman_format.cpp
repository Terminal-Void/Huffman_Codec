#include "huffman_format.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Node = HuffNode<Byte>;

namespace {
    constexpr std::array<char, 4> kMagic = {'H', 'U', 'F', '1'};

    // GenHuffTreeByConfigFile 使用 FILE*，这里用 unique_ptr 接管关闭操作
    struct FileCloser {
        void operator()(FILE *file) const noexcept {
            if (file != nullptr) {
                std::fclose(file);
            }
        }
    };

    using FilePtr = std::unique_ptr<FILE, FileCloser>;

    ArchivePaths make_paths_from_base(const fs::path &base) {
        fs::path archive = base;
        archive += ".huf";

        fs::path shape = base;
        shape += ".config1.txt";

        fs::path leaves = base;
        leaves += ".config2.txt";

        const std::string decoded_name = base.stem().string() + ".decoded" + base.extension().string();

        return {archive,shape,leaves,base.parent_path() / decoded_name};
    }

    void write_uint64(std::ostream &output, const std::uint64_t value) {
        // 文件头统一使用小端序
        for (int i = 0; i < 8; ++i) {
            output.put(static_cast<char>((value >> (i * 8)) & 0xffU));
        }

        if (!output) {
            throw std::ios_base::failure("写入压缩文件头失败");
        }
    }

    // 按字节手工拼装小端序 uint64_t，与 write_uint64 对称
    // 工程上不直接对 ArchiveHeader 执行 reinterpret_cast<char*> 整块二进制读写，
    // 原因在于两方面：一是消除不同编译器结构体内存对齐填充（padding）导致的布局差异，二是确保压缩文件在大小端不同架构机器间可移植
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

    // 使用显式栈迭代还原哈夫曼树，而非传统的先序递归建树
    // 原因在于若传入受损或恶意构造的超长 config1.txt，递归建树的调用栈深度随内部节点数线性增长，极易触发栈溢出崩溃；
    // 迭代栈分配在堆上且空间受限于树高与最大叶节点上界（256），安全性更好
    std::unique_ptr<Node> parse_tree_node(FILE *shape, FILE *leaves) {
        // 若任意编码符号集至多包含 256 个互异单字节，则合法哈夫曼树至多有 256 个叶节点与 255 个内部节点
        constexpr std::size_t kMaxLeaves = 256;

        // 每个栈帧对应一个尚未构造完毕的内部节点：先序遍历永远先构造左子树再构造右子树，
        // 故栈帧中只需暂存已建好的左子树句柄；待右子树完成时立即弹出栈顶合并为 IntlNode
        struct BuildFrame {
            std::unique_ptr<Node> left;
        };

        std::vector<BuildFrame> stack;
        std::unique_ptr<Node> root;
        // 循环不变量 open_slots：表示当前树中尚未闭合的悬挂子分支数（初始根节点自身计为 1 个待填分支）
        // - 读到内部节点 '1'：填补当前 1 个待填分支并新产生左、右 2 个待填分支，待填总数加 1（++open_slots）；
        // - 读到叶节点 '0'  ：直接闭合当前分支（--open_slots），不再产生子节点
        // 当且仅当 open_slots 减至 0 时，整棵满二叉树的所有分支均构造完成
        std::size_t open_slots = 1;
        std::size_t leaves_seen = 0;

        // config1 按先序保存树形：1 表示内部节点，0 表示叶节点
        while (open_slots > 0) {
            const int marker = std::fgetc(shape);
            if (marker == EOF) {
                throw std::runtime_error("树结构文件提前结束");
            }

            if (marker == '1') {
                ++open_slots;
                // 即便后续输入全部为叶节点 '0'，最终总叶节点数也至少等于 leaves_seen + open_slots；
                // 一旦超过 256 即提前判定 config1 结构非法并终止解析，防止超大畸形文件持续压栈耗尽内存
                if (leaves_seen + open_slots > kMaxLeaves) {
                    throw std::runtime_error("树结构规模超过最大叶节点数限制");
                }
                stack.push_back(BuildFrame{nullptr});
                continue;
            }

            if (marker != '0') {
                throw std::runtime_error("树结构文件包含非法标记");
            }

            const int value = std::fgetc(leaves);
            if (value == EOF) {
                throw std::runtime_error("叶节点信息文件提前结束");
            }

            --open_slots;
            ++leaves_seen;

            // 解压过程只需沿树分支判断 0/1 走向，节点权重不参与解码（按实验要求 config2 也不存储频率信息），此处叶节点权重统一填 0
            std::unique_ptr<Node> current = std::make_unique<LeafNode<Byte> >(
                static_cast<Byte>(value),
                0
            );

            // 自底向上合并已闭合的子树：
            // 若栈顶内部节点尚未挂载左子树，则把 current 设为左子树并跳出内层循环，继续读入其右子树的先序片段；
            // 若栈顶左子树已存在，说明 current 正好构成该内部节点的右子树，两者合并为 IntlNode<Byte> 并弹栈，继续向父层闭合
            while (!stack.empty()) {
                if (stack.back().left == nullptr) {
                    stack.back().left = std::move(current);
                    break;
                }

                current = std::make_unique<IntlNode<Byte> >(
                    std::move(stack.back().left),
                    std::move(current)
                );
                stack.pop_back();
            }

            if (stack.empty()) {
                root = std::move(current);
            }
        }

        if (root == nullptr) {
            throw std::logic_error("树结构解析结果为空");
        }
        return root;
    }

    void write_tree_config_recursive(
        Node *root,
        std::ostream &shape,
        std::ostream &leaves
    ) {
        if (root->isLeaf()) {
            shape.put('0');
            const auto *leaf = dynamic_cast<LeafNode<Byte> *>(root);
            if (leaf == nullptr) {
                throw std::logic_error("叶节点类型与 isLeaf() 不一致");
            }

            leaves.put(static_cast<char>(leaf->val()));
            return;
        }

        shape.put('1');
        const auto *internal = dynamic_cast<IntlNode<Byte> *>(root);
        if (internal == nullptr) {
            throw std::logic_error("内部节点类型与 isLeaf() 不一致");
        }

        write_tree_config_recursive(internal->left(), shape, leaves);
        write_tree_config_recursive(internal->right(), shape, leaves);
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

    return {read_uint64(input),read_uint64(input)};
}

void write_tree_config(
    const HuffTree<Byte> &tree,
    std::ostream &shape,
    std::ostream &leaves
) {
    write_tree_config_recursive(tree.root(), shape, leaves);
}

TreeHandle GenHuffTreeByConfigFile(
    FILE *shape_file,
    FILE *leaves_file
) {
    if (shape_file == nullptr || leaves_file == nullptr) {
        throw std::invalid_argument("树配置文件句柄不能为空");
    }

    auto root = parse_tree_node(shape_file, leaves_file);

    // 校验完整读取
    // 整棵满二叉树构造结束时，两个配置文件也须同步抵达 EOF
    // 若仍遗留多余字节（或发生 I/O 读取错误），说明树结构与叶节点文件相互不匹配
    if (std::fgetc(shape_file) != EOF || std::ferror(shape_file)) {
        throw std::runtime_error("树结构文件包含多余内容");
    }

    if (std::fgetc(leaves_file) != EOF || std::ferror(leaves_file)) {
        throw std::runtime_error("叶节点信息文件包含多余内容");
    }

    return std::make_unique<HuffTree<Byte> >(std::move(root));
}

TreeHandle load_tree_config(const fs::path &shape_path,const fs::path &leaves_path) {
    // 虽然配置文件按惯例以 .txt 结尾，此处仍必须使用 "rb"（二进制只读）模式打开
    // 原因在于本程序支持压缩任意 0~255 字节，config2.txt 中完全可能写入 0x1A（Ctrl+Z）、'\r'（0x0D）与 '\n'（0x0A）：
    // 若在 Windows 上使用文本模式 "r"，遇到 0x1A 会被 C 运行时误判为提前 EOF，遇到 "\r\n" 会被静默转换并丢弃 '\r'，导致叶节点数据失真
    const FilePtr shape_file(std::fopen(shape_path.string().c_str(), "rb"));
    if (!shape_file) {
        throw std::runtime_error("无法打开树结构文件：" + shape_path.string());
    }
    const FilePtr leaves_file(std::fopen(leaves_path.string().c_str(), "rb"));
    if (!leaves_file) {
        throw std::runtime_error("无法打开叶节点信息文件：" + leaves_path.string());
    }

    return GenHuffTreeByConfigFile(
        shape_file.get(),
        leaves_file.get()
    );
}
